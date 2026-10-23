/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#include <string>
#include <string.h>
#include <stdlib.h>
#include "ssp-mdns.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <functional>
#include <QObject>
#include <mutex>
#include <obs-module.h>
#include <obs.h>
#include <util/platform.h>
#include <util/threading.h>
#include <chrono>
#include <thread>

#include "obs-ssp.h"
#include "imf/ISspClient.h"
#include "imf/threadloop.h"

#include "ssp-controller.h"
#include "ssp-settings.h"
#include "ssp-client-iso.h"
#include "VFrameQueue.h"

extern "C" {
#include "ffmpeg-decode.h"
}

#define PROP_SOURCE_IP "ssp_source_ip"
#define PROP_CUSTOM_SOURCE_IP "ssp_custom_source_ip"
#define PROP_NO_CHECK "ssp_no_check"
#define PROP_CHECK_IP "ssp_check_ip"

#define PROP_CUSTOM_VALUE "\x01\x02custom"

#define PROP_HW_ACCEL "ssp_recv_hw_accel"
#define PROP_SYNC "ssp_sync"
#define PROP_LATENCY "latency"
#define PROP_VIDEO_RANGE "video_range"
#define PROP_EXP_WAIT_I "exp_wait_i_frame"

#define PROP_BW_HIGHEST 0
#define PROP_BW_LOWEST 1
#define PROP_BW_AUDIO_ONLY 2

#define PROP_SYNC_INTERNAL 0
#define PROP_SYNC_SSP_TIMESTAMP 1

#define PROP_LATENCY_NORMAL 0
#define PROP_LATENCY_LOW 1

#define PROP_LED_TALLY "led_as_tally_light"
#define PROP_RESOLUTION "ssp_resolution"
#define PROP_FRAME_RATE "ssp_frame_rate"
#define PROP_LOW_NOISE "ssp_low_noise"
#define PROP_BITRATE "ssp_bitrate"
#define PROP_STREAM_INDEX "ssp_stream_index"
#define PROP_ENCODER "ssp_encoding"
#define PROP_CAMERA_BITRATE "ssp_camera_bitrate_kbps"
#define PROP_CAMERA_STATUS "ssp_camera_status"

#define SSP_IP_DIRECT "10.98.32.1"
#define SSP_IP_WIFI "10.98.33.1"
#define SSP_IP_USB "172.18.18.1"

using namespace std::placeholders;

struct ssp_source;

struct ssp_connection {
	SSPClientIso *client;
	ffmpeg_decode vdecoder;
	uint32_t width;
	uint32_t height;
	AVCodecID vformat;
	obs_source_frame2 frame;

	ffmpeg_decode adecoder;
	uint32_t sample_size;
	uint32_t audio_channels;
	uint64_t decoded_video_frames;
	uint64_t decoded_audio_frames;
	AVCodecID aformat;
	obs_source_audio audio;

	VFrameQueue *queue;
	bool running;
	int i_frame_shown;

	// copy from ssp_source
	char *source_ip;
	int hwaccel;
	int bitrate;
	int wait_i_frame;
	int sync_mode;
	obs_source_t *source;
	// not used
	int video_range;

	pthread_mutex_t lck;
};

struct ssp_source {
	obs_source_t *source;
	CameraStatus *cameraStatus;

	int sync_mode;
	int video_range;
	int hwaccel;
	int bitrate;
	int wait_i_frame;
	int tally;

	bool do_check;
	bool no_check;
	bool ip_checked;

	const char *source_ip;
	ssp_connection *conn;
};

static void ssp_conn_start(ssp_connection *s);
static void ssp_conn_stop(ssp_connection *s);
static void ssp_stop(ssp_source *s);
static void ssp_start(ssp_source *s);
void *thread_ssp_reconnect(void *data);

static void ssp_video_data_enqueue(struct imf::SspH264Data *video, ssp_connection *s)
{
	if (!s->running) {
		return;
	}
	if (!s->queue) {
		return;
	}
	s->queue->enqueue(*video, video->pts, video->type == 5);
}

static void ssp_on_video_data(struct imf::SspH264Data *video, ssp_connection *s)
{
	if (!s->running) {
		return;
	}
	if (!ffmpeg_decode_valid(&s->vdecoder)) {
		assert(s->vformat == AV_CODEC_ID_H264 || s->vformat == AV_CODEC_ID_HEVC);
		if (ffmpeg_decode_init(&s->vdecoder, s->vformat, s->hwaccel) < 0) {
			ssp_blog(LOG_WARNING, "Could not initialize video decoder");
			return;
		}
	}
	if (s->wait_i_frame && !s->i_frame_shown) {
		if (video->type == 5) {
			s->i_frame_shown = true;
		} else {
			return;
		}
	}

	const uint64_t decode_start_ns = os_gettime_ns();
	long long ts = video->pts;
	bool got_output;
	bool success = ffmpeg_decode_video(&s->vdecoder, video->data, video->len, &ts, VIDEO_CS_DEFAULT,
					   VIDEO_RANGE_PARTIAL, &s->frame, &got_output);
	if (!success) {
		ssp_blog(LOG_WARNING, "Error decoding video");
		return;
	}

	if (got_output) {
		if (s->sync_mode == PROP_SYNC_INTERNAL) {
			s->frame.timestamp = os_gettime_ns();
		} else {
			s->frame.timestamp = (uint64_t)ts * 1000;
		}
		//        if (flip)
		//            frame.flip = !frame.flip;
		const uint64_t decode_end_ns = os_gettime_ns();
		obs_source_output_video2(s->source, &s->frame);
		const uint64_t submit_end_ns = os_gettime_ns();
		if (++s->decoded_video_frames == 1 || s->decoded_video_frames % 300 == 0)
			ssp_blog(
				LOG_INFO,
				"Video decoded: %llu frames, %ux%u format=%d; decode-call=%.3f ms OBS-submit=%.3f ms (excludes capture/network/display)",
				(unsigned long long)s->decoded_video_frames, s->frame.width, s->frame.height,
				s->frame.format, (decode_end_ns - decode_start_ns) / 1000000.0,
				(submit_end_ns - decode_end_ns) / 1000000.0);
	}
}

static void ssp_on_audio_data(struct imf::SspAudioData *audio, ssp_connection *s)
{
	if (!s->running) {
		return;
	}
	if (!ffmpeg_decode_valid(&s->adecoder)) {
		if (s->aformat == AV_CODEC_ID_NONE)
			return;
		if (ffmpeg_decode_audio_init(&s->adecoder, s->aformat, s->audio.samples_per_sec, s->audio_channels) <
		    0) {
			ssp_blog(LOG_WARNING, "Could not initialize audio decoder");
			return;
		}
	}
	uint8_t *data = audio->data;
	size_t size = audio->len;
	bool got_output = false;
	uint64_t offset_frames = 0;
	const uint64_t now_ns = os_gettime_ns();
	do {
		bool success = ffmpeg_decode_audio(&s->adecoder, data, size, &s->audio, &got_output);
		if (!success) {
			ssp_blog(LOG_WARNING, "Error decoding audio");
			return;
		}
		if (got_output) {
			s->audio.timestamp = ssp_audio_timestamp_ns(s->sync_mode == PROP_SYNC_INTERNAL, audio->pts,
								    now_ns, offset_frames, s->audio.samples_per_sec);
			offset_frames += s->audio.frames;
			obs_source_output_audio(s->source, &s->audio);
			if (++s->decoded_audio_frames == 1)
				ssp_blog(LOG_INFO, "Audio decoded: %u Hz, %u samples, format=%d speakers=%d",
					 s->audio.samples_per_sec, s->audio.frames, s->audio.format, s->audio.speakers);
		} else {
			break;
		}

		size = 0;
		data = nullptr;
	} while (got_output);
}

static void ssp_on_meta_data(struct imf::SspVideoMeta *v, struct imf::SspAudioMeta *a, struct imf::SspMeta *m,
			     ssp_connection *s)
{
	ssp_blog(LOG_INFO, "ssp v meta: encoder: %u, gop:%u, height:%u, timescale:%u, unit:%u, width:%u", v->encoder,
		 v->gop, v->height, v->timescale, v->unit, v->width);
	ssp_blog(
		LOG_INFO,
		"ssp a meta: uinit: %u, timescale:%u, encoder:%u, bitrate:%u, channel:%u, sample_rate:%u, sample_size:%u",
		a->unit, a->timescale, a->encoder, a->bitrate, a->channel, a->sample_rate, a->sample_size);
	ssp_blog(LOG_INFO, "ssp i meta: pts_is_wall_clock: %u, tc_drop_frame:%u, timecode:%u,", m->pts_is_wall_clock,
		 m->tc_drop_frame, m->timecode);
	s->vformat = v->encoder == VIDEO_ENCODER_H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_H265;
	s->frame.width = v->width;
	s->frame.height = v->height;
	s->sample_size = a->sample_size;
	s->audio.samples_per_sec = a->sample_rate;
	s->audio_channels = a->channel;
	s->aformat = a->encoder == AUDIO_ENCODER_AAC   ? AV_CODEC_ID_AAC
		     : a->encoder == AUDIO_ENCODER_PCM ? AV_CODEC_ID_PCM_S16LE
						       : AV_CODEC_ID_NONE;
}

static void ssp_on_disconnected(ssp_connection *s)
{
	ssp_blog(LOG_INFO, "ssp device disconnected.");
	pthread_t thread;
	if (s->running) {
		ssp_blog(LOG_INFO, "still running, reconnect...");
		pthread_create(&thread, nullptr, thread_ssp_reconnect, (void *)s);
		pthread_detach(thread);
	}
}

static void ssp_on_exception(int code, const char *description, ssp_connection *s)
{
	UNUSED_PARAMETER(s);
	ssp_blog(LOG_ERROR, "ssp exception %d: %s", code, description);
	//s->running = false;
}

static void ssp_start(ssp_source *s)
{
	auto conn = (ssp_connection *)bzalloc(sizeof(ssp_connection));
	conn->source = s->source;
	conn->source_ip = strdup(s->source_ip);
	conn->wait_i_frame = s->wait_i_frame;
	conn->hwaccel = s->hwaccel;
	conn->bitrate = s->bitrate;
	conn->sync_mode = s->sync_mode;
	conn->video_range = s->video_range;
	pthread_mutex_init(&conn->lck, nullptr);

	s->conn = conn;
	ssp_conn_start(conn);
}

static void ssp_conn_stop(ssp_connection *conn)
{
	ssp_blog(LOG_INFO, "Stopping ssp client...");
	pthread_mutex_lock(&conn->lck);
	conn->running = false;
	auto client = conn->client;
	auto queue = conn->queue;

	if (client) {
		client->Stop();
		delete client;
	}
	if (queue) {
		queue->stop();
		delete queue;
	}

	ssp_blog(LOG_INFO, "SSP client stopped.");

	if (ffmpeg_decode_valid(&conn->adecoder)) {
		ffmpeg_decode_free(&conn->adecoder);
	}
	if (ffmpeg_decode_valid(&conn->vdecoder)) {
		ffmpeg_decode_free(&conn->vdecoder);
	}

	ssp_blog(LOG_INFO, "SSP conn stopped.");
	pthread_mutex_unlock(&conn->lck);
}

static void ssp_stop(ssp_source *s)
{
	if (!s) {
		return;
	}
	auto conn = s->conn;
	s->conn = nullptr;
	if (!conn) {
		return;
	}
	ssp_conn_stop(conn);
	free((void *)conn->source_ip);
	bfree(conn);
}

static void ssp_conn_start(ssp_connection *s)
{
	ssp_blog(LOG_INFO, "Starting ssp client...");
	assert(s->client == nullptr);
	assert(s->source != nullptr);

	std::string ip = s->source_ip;
	ssp_blog(LOG_INFO, "target ip: %s", s->source_ip);
	ssp_blog(LOG_INFO, "source bitrate: %d", s->bitrate);
	if (strlen(s->source_ip) == 0) {
		return;
	}
	pthread_mutex_lock(&s->lck);
	s->client = new SSPClientIso(ip, s->bitrate / 8);
	s->client->setOnH264DataCallback(std::bind(ssp_video_data_enqueue, _1, s));
	s->client->setOnAudioDataCallback(std::bind(ssp_on_audio_data, _1, s));
	s->client->setOnMetaCallback(std::bind(ssp_on_meta_data, _1, _2, _3, s));
	s->client->setOnConnectionConnectedCallback([]() { ssp_blog(LOG_INFO, "ssp connected."); });
	s->client->setOnDisconnectedCallback(std::bind(ssp_on_disconnected, s));
	s->client->setOnExceptionCallback(std::bind(ssp_on_exception, _1, _2, s));

	assert(s->queue == nullptr);
	s->queue = new VFrameQueue;
	s->queue->setFrameCallback(std::bind(ssp_on_video_data, _1, s));

	s->queue->start();
	emit s->client->Start();
	s->running = true;
	pthread_mutex_unlock(&s->lck);
	ssp_blog(LOG_INFO, "SSP client started.");
}

void *thread_ssp_reconnect(void *data)
{
	auto conn = (ssp_connection *)data;
	ssp_blog(LOG_INFO, "Stopping ssp client...");
	pthread_mutex_lock(&conn->lck);
	if (!conn->running) {
		pthread_mutex_unlock(&conn->lck);
		return nullptr;
	}
	auto client = conn->client;
	auto queue = conn->queue;

	if (client) {
		client->Stop();
		delete client;
	}
	if (queue) {
		queue->stop();
		delete queue;
	}

	ssp_blog(LOG_INFO, "SSP client stopped.");

	if (ffmpeg_decode_valid(&conn->adecoder)) {
		ffmpeg_decode_free(&conn->adecoder);
	}
	if (ffmpeg_decode_valid(&conn->vdecoder)) {
		ffmpeg_decode_free(&conn->vdecoder);
	}

	ssp_blog(LOG_INFO, "SSP conn stopped.");

	ssp_blog(LOG_INFO, "Starting ssp client...");
	assert(conn->client == nullptr);
	assert(conn->source != nullptr);

	std::string ip = conn->source_ip;
	ssp_blog(LOG_INFO, "target ip: %s", conn->source_ip);
	ssp_blog(LOG_INFO, "source bitrate: %d", conn->bitrate);
	if (strlen(conn->source_ip) == 0) {
		pthread_mutex_unlock(&conn->lck);
		return nullptr;
	}
	conn->client = new SSPClientIso(ip, conn->bitrate / 8);
	conn->client->setOnH264DataCallback(std::bind(ssp_video_data_enqueue, _1, conn));
	conn->client->setOnAudioDataCallback(std::bind(ssp_on_audio_data, _1, conn));
	conn->client->setOnMetaCallback(std::bind(ssp_on_meta_data, _1, _2, _3, conn));
	conn->client->setOnConnectionConnectedCallback([]() { ssp_blog(LOG_INFO, "ssp connected."); });
	conn->client->setOnDisconnectedCallback(std::bind(ssp_on_disconnected, conn));
	conn->client->setOnExceptionCallback(std::bind(ssp_on_exception, _1, _2, conn));

	assert(conn->queue == nullptr);
	conn->queue = new VFrameQueue;
	conn->queue->setFrameCallback(std::bind(ssp_on_video_data, _1, conn));

	conn->queue->start();
	emit conn->client->Start();
	pthread_mutex_unlock(&conn->lck);
	ssp_blog(LOG_INFO, "SSP client started.");

	return nullptr;
}

static obs_source_frame *blank_video_frame()
{
	obs_source_frame *frame = obs_source_frame_create(VIDEO_FORMAT_NONE, 0, 0);
	frame->timestamp = os_gettime_ns();
	return frame;
}

const char *ssp_source_getname(void *data)
{
	UNUSED_PARAMETER(data);
	return obs_module_text("SSPPlugin.SSPSourceName");
}

static void read_camera_settings(ssp_source *s)
{
	s->cameraStatus->refreshAll([=](bool ok) {
		obs_data_t *settings = obs_source_get_settings(s->source);
		if (ok) {
			obs_data_set_string(settings, PROP_RESOLUTION,
					    s->cameraStatus->current_resolution.toUtf8().constData());
			obs_data_set_string(settings, PROP_FRAME_RATE,
					    s->cameraStatus->current_framerate.toUtf8().constData());
			obs_data_set_string(settings, PROP_ENCODER,
					    ssp_camera_encoder(s->cameraStatus->current_streamInfo.encoderType_)
						    .toUtf8()
						    .constData());
			obs_data_set_int(settings, PROP_STREAM_INDEX,
					 s->cameraStatus->current_stream == "Stream1" ? 1 : 0);
			obs_data_set_int(settings, PROP_CAMERA_BITRATE, s->cameraStatus->current_streamInfo.bitrate_);
		}
		obs_data_set_string(settings, PROP_CAMERA_STATUS,
				    ok ? "Camera settings read; playback never changes camera settings automatically."
				       : "Camera controls unavailable; SSP playback can still connect.");
		obs_data_release(settings);
		obs_source_update_properties(s->source);
	});
}

static bool read_camera_callback(obs_properties_t *, obs_property_t *, void *data)
{
	read_camera_settings(static_cast<ssp_source *>(data));
	return false;
}

static bool apply_camera_callback(obs_properties_t *, obs_property_t *, void *data)
{
	auto s = static_cast<ssp_source *>(data);
	ssp_stop(s);
	obs_data_t *settings = obs_source_get_settings(s->source);
	auto resolution = QString::fromUtf8(obs_data_get_string(settings, PROP_RESOLUTION));
	auto fps = QString::fromUtf8(obs_data_get_string(settings, PROP_FRAME_RATE));
	auto encoder = QString::fromUtf8(obs_data_get_string(settings, PROP_ENCODER));
	int stream_index = (int)obs_data_get_int(settings, PROP_STREAM_INDEX);
	int bitrate = (int)obs_data_get_int(settings, PROP_CAMERA_BITRATE);
	obs_data_release(settings);
	s->cameraStatus->setStream(
		stream_index, resolution, false, fps, encoder, bitrate, [=](bool ok, QString reason) {
			ssp_blog(ok ? LOG_INFO : LOG_WARNING, "Camera settings: %s", reason.toUtf8().constData());
			obs_data_t *settings = obs_source_get_settings(s->source);
			obs_data_set_string(settings, PROP_CAMERA_STATUS, reason.toUtf8().constData());
			obs_data_release(settings);
			ssp_start(s);
			if (ok)
				read_camera_settings(s);
			else
				obs_source_update_properties(s->source);
		});
	return false;
}

bool source_ip_modified(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	auto s = (struct ssp_source *)data;
	const char *source_ip = obs_data_get_string(settings, PROP_SOURCE_IP);
	s->ip_checked = false;
	if (strcmp(source_ip, PROP_CUSTOM_VALUE) == 0) {
		obs_property_t *custom_ip = obs_properties_get(props, PROP_CUSTOM_SOURCE_IP);
		obs_property_t *check_ip = obs_properties_get(props, PROP_CHECK_IP);
		obs_property_set_visible(property, false);
		obs_property_set_visible(custom_ip, true);
		obs_property_set_visible(check_ip, true);
		return true;
	}
	if (s->cameraStatus->getIp() == source_ip) {
		return false;
	}
	s->cameraStatus->setIp(source_ip);
	read_camera_settings(s);
	return false;
}

static bool custom_ip_modify_callback(void *data, obs_properties_t *props, obs_property_t *property,
				      obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	auto s = (struct ssp_source *)data;
	if (s->ip_checked || !s->do_check) {
		s->ip_checked = false;
		ssp_blog(LOG_INFO, "ip modified, no need to check.");
		return false;
	}
	s->do_check = false;
	ssp_blog(LOG_INFO, "ip modified, need to check.");
	auto ip = obs_data_get_string(settings, PROP_CUSTOM_SOURCE_IP);
	if (strcmp(ip, "") == 0) {
		return false;
	}
	s->cameraStatus->setIp(ip);
	read_camera_settings(s);

	ssp_blog(LOG_INFO, "ip check queued.");
	return false;
}

static bool resolution_modify_callback(void *data, obs_properties_t *props, obs_property_t *property,
				       obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	auto s = (struct ssp_source *)data;
	auto framerates = obs_properties_get(props, PROP_FRAME_RATE);
	obs_property_list_clear(framerates);

	if (s->cameraStatus->model.isEmpty()) {
		return false;
	}

	auto resolution = ssp_camera_resolution(QString::fromUtf8(obs_data_get_string(settings, PROP_RESOLUTION)),
						obs_data_get_bool(settings, PROP_LOW_NOISE));
	QStringList fallback;
	for (const auto &fps : s->cameraStatus->framerates)
		fallback.append(fps);
	for (const auto &fps : ssp_camera_frame_rates(s->cameraStatus->movie_formats, resolution, fallback))
		obs_property_list_add_string(framerates, fps.toUtf8().constData(), fps.toUtf8().constData());
	return true;
}

static bool check_ip_callback(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	auto s = (struct ssp_source *)data;
	s->do_check = true;
	obs_source_update_properties(s->source);
	return false;
}

obs_properties_t *ssp_source_getproperties(void *data)
{
	char nametext[256];
	auto s = (struct ssp_source *)data;

	obs_properties_t *props = obs_properties_create();
	// Playback updates are safe immediately; camera writes require the Apply button.

	obs_property_t *source_ip = obs_properties_add_list(props, PROP_SOURCE_IP,
							    obs_module_text("SSPPlugin.SourceProps.SourceIp"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	snprintf(nametext, 256, "%s (%s)", obs_module_text("SSPPlugin.IP.Fixed"), SSP_IP_DIRECT);
	obs_property_list_add_string(source_ip, nametext, SSP_IP_DIRECT);

	snprintf(nametext, 256, "%s (%s)", obs_module_text("SSPPlugin.IP.Wifi"), SSP_IP_WIFI);
	obs_property_list_add_string(source_ip, nametext, SSP_IP_WIFI);

	snprintf(nametext, 256, "%s (%s)", obs_module_text("SSPPlugin.IP.USB"), SSP_IP_USB);
	obs_property_list_add_string(source_ip, nametext, SSP_IP_USB);

	int count = 0;

	SspMDnsIterator iter;
	while (iter.hasNext()) {
		ssp_device_item *item = iter.next();
		if (item == nullptr) {
			continue;
		}
		snprintf(nametext, 256, "%s (%s)", item->device_name.c_str(), item->ip_address.c_str());
		obs_property_list_add_string(source_ip, nametext, item->ip_address.c_str());
		++count;
	}

	if (count == 0)
		obs_property_list_add_string(source_ip, obs_module_text("SSPPlugin.SourceProps.NotFound"), "");
	obs_property_list_add_string(source_ip, obs_module_text("SSPPlugin.SourceProps.Custom"), PROP_CUSTOM_VALUE);

	obs_property_t *custom_source_ip = obs_properties_add_text(
		props, PROP_CUSTOM_SOURCE_IP, obs_module_text("SSPPlugin.SourceProps.SourceIp"), OBS_TEXT_DEFAULT);

	obs_properties_add_bool(props, PROP_NO_CHECK, obs_module_text("SSPPlugin.SourceProps.DontCheck"));

	obs_property_t *check_button = obs_properties_add_button2(
		props, PROP_CHECK_IP, obs_module_text("SSPPlugin.SourceProps.CheckIp"), check_ip_callback, data);

	obs_property_set_visible(custom_source_ip, false);
	obs_property_set_visible(check_button, false);

	obs_property_set_modified_callback2(source_ip, source_ip_modified, data);
	obs_property_set_modified_callback2(custom_source_ip, custom_ip_modify_callback, data);

	obs_property_t *sync_modes = obs_properties_add_list(props, PROP_SYNC,
							     obs_module_text("SSPPlugin.SourceProps.Sync"),
							     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(sync_modes, obs_module_text("SSPPlugin.SyncMode.Internal"), PROP_SYNC_INTERNAL);
	obs_property_list_add_int(sync_modes, obs_module_text("SSPPlugin.SyncMode.SSPTimestamp"),
				  PROP_SYNC_SSP_TIMESTAMP);

	obs_properties_add_bool(props, PROP_HW_ACCEL, obs_module_text("SSPPlugin.SourceProps.HWAccel"));

	obs_property_t *latency_modes = obs_properties_add_list(props, PROP_LATENCY,
								obs_module_text("SSPPlugin.SourceProps.Latency"),
								OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(latency_modes, obs_module_text("SSPPlugin.SourceProps.Latency.Normal"),
				  PROP_LATENCY_NORMAL);
	obs_property_list_add_int(latency_modes, obs_module_text("SSPPlugin.SourceProps.Latency.Low"),
				  PROP_LATENCY_LOW);

	obs_property_t *encoders = obs_properties_add_list(props, PROP_ENCODER,
							   obs_module_text("SSPPlugin.SourceProps.Encoder"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const auto &encoder : s->cameraStatus->encoders)
		obs_property_list_add_string(encoders, encoder.toUtf8().constData(), encoder.toUtf8().constData());

	obs_properties_add_bool(props, PROP_EXP_WAIT_I, obs_module_text("SSPPlugin.SourceProps.WaitIFrame"));

	obs_property_t *resolutions = obs_properties_add_list(props, PROP_RESOLUTION,
							      obs_module_text("SSPPlugin.SourceProps.Resolution"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const auto &resolution : s->cameraStatus->resolutions)
		obs_property_list_add_string(resolutions, resolution.toUtf8().constData(),
					     resolution.toUtf8().constData());

	obs_property_set_modified_callback2(resolutions, resolution_modify_callback, data);

	obs_property_t *framerate = obs_properties_add_list(props, PROP_FRAME_RATE,
							    obs_module_text("SSPPlugin.SourceProps.FrameRate"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_properties_add_int(props, PROP_CAMERA_BITRATE, obs_module_text("SSPPlugin.SourceProps.CameraBitrate"), 1,
			       1000000, 1000);
	auto stream = obs_properties_add_list(props, PROP_STREAM_INDEX,
					      obs_module_text("SSPPlugin.SourceProps.StreamIndex"), OBS_COMBO_TYPE_LIST,
					      OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(stream, "Stream0 (main)", 0);
	obs_property_list_add_int(stream, "Stream1 (secondary)", 1);
	obs_properties_add_text(props, PROP_CAMERA_STATUS, obs_module_text("SSPPlugin.SourceProps.CameraStatus"),
				OBS_TEXT_INFO);
	obs_properties_add_button2(props, "ssp_camera_read", obs_module_text("SSPPlugin.SourceProps.ReadCamera"),
				   read_camera_callback, data);
	obs_properties_add_button2(props, "ssp_camera_apply", obs_module_text("SSPPlugin.SourceProps.ApplyCamera"),
				   apply_camera_callback, data);

	obs_property_t *tally =
		obs_properties_add_bool(props, PROP_LED_TALLY, obs_module_text("SSPPlugin.SourceProps.LedAsTally"));

	UNUSED_PARAMETER(resolutions);
	UNUSED_PARAMETER(encoders);
	UNUSED_PARAMETER(framerate);
	UNUSED_PARAMETER(tally);

	return props;
}

void ssp_source_getdefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, PROP_SYNC, PROP_SYNC_SSP_TIMESTAMP);
	obs_data_set_default_int(settings, PROP_LATENCY, PROP_LATENCY_LOW);
	obs_data_set_default_string(settings, PROP_SOURCE_IP, "");
	obs_data_set_default_string(settings, PROP_CUSTOM_SOURCE_IP, "");
	obs_data_set_default_int(settings, PROP_BITRATE, 20);
	obs_data_set_default_bool(settings, PROP_HW_ACCEL, false);
	obs_data_set_default_bool(settings, PROP_EXP_WAIT_I, true);
	obs_data_set_default_bool(settings, PROP_LED_TALLY, false);
	obs_data_set_default_bool(settings, PROP_LOW_NOISE, false);
	obs_data_set_default_string(settings, PROP_ENCODER, "H264");
	obs_data_set_default_string(settings, PROP_FRAME_RATE, "29.97");
}

void ssp_source_update(void *data, obs_data_t *settings)
{
	auto s = (struct ssp_source *)data;

	const char *source_ip = obs_data_get_string(settings, PROP_SOURCE_IP);
	if (strcmp(source_ip, PROP_CUSTOM_VALUE) == 0)
		source_ip = obs_data_get_string(settings, PROP_CUSTOM_SOURCE_IP);
	obs_source_set_async_unbuffered(s->source, obs_data_get_int(settings, PROP_LATENCY) == PROP_LATENCY_LOW);
	s->tally = obs_data_get_bool(settings, PROP_LED_TALLY);
	if (s->conn && s->source_ip && strcmp(s->source_ip, source_ip) == 0 &&
	    s->hwaccel == obs_data_get_bool(settings, PROP_HW_ACCEL) &&
	    s->sync_mode == obs_data_get_int(settings, PROP_SYNC) &&
	    s->wait_i_frame == obs_data_get_bool(settings, PROP_EXP_WAIT_I))
		return;
	ssp_stop(s);

	s->hwaccel = obs_data_get_bool(settings, PROP_HW_ACCEL);

	s->sync_mode = (int)obs_data_get_int(settings, PROP_SYNC);
	source_ip = obs_data_get_string(settings, PROP_SOURCE_IP);
	if (strcmp(source_ip, PROP_CUSTOM_VALUE) == 0) {
		source_ip = obs_data_get_string(settings, PROP_CUSTOM_SOURCE_IP);
	}
	if (strlen(source_ip) == 0) {
		return;
	}

	if (s->source_ip) {
		free((void *)s->source_ip);
	}
	s->source_ip = strdup(source_ip);

	// Set the IP of our camera from the configuration (used to build the url)
	const bool camera_changed = s->cameraStatus->getIp() != s->source_ip;
	s->cameraStatus->setIp(s->source_ip);
	if (camera_changed)
		read_camera_settings(s);

	const bool is_unbuffered = (obs_data_get_int(settings, PROP_LATENCY) == PROP_LATENCY_LOW);
	obs_source_set_async_unbuffered(s->source, is_unbuffered);

	s->wait_i_frame = obs_data_get_bool(settings, PROP_EXP_WAIT_I);

	s->tally = obs_data_get_bool(settings, PROP_LED_TALLY);

	s->bitrate = (int)obs_data_get_int(settings, PROP_BITRATE) * 1000000;
	ssp_start(s);
}

void ssp_source_shown(void *data)
{
	auto s = (struct ssp_source *)data;
	if (s->tally) {
		s->cameraStatus->setLed(true);
	}
	ssp_blog(LOG_INFO, "ssp source shown.");
}

void ssp_source_hidden(void *data)
{
	auto s = (struct ssp_source *)data;
	if (s->tally) {
		s->cameraStatus->setLed(false);
	}
	ssp_blog(LOG_INFO, "ssp source hidden.");
}

void ssp_source_activated(void *data)
{
	UNUSED_PARAMETER(data);
	ssp_blog(LOG_INFO, "ssp source activated.");
}

void ssp_source_deactivated(void *data)
{
	UNUSED_PARAMETER(data);
	ssp_blog(LOG_INFO, "ssp source deactivated.");
}

void *ssp_source_create(obs_data_t *settings, obs_source_t *source)
{
	auto s = (struct ssp_source *)bzalloc(sizeof(struct ssp_source));
	s->source = source;
	s->do_check = false;
	s->no_check = false;
	s->ip_checked = false;
	s->cameraStatus = new CameraStatus();
	s->source_ip = nullptr;
	ssp_source_update(s, settings);
	return s;
}

void ssp_source_destroy(void *data)
{
	auto s = (struct ssp_source *)data;
	ssp_blog(LOG_INFO, "destroying source...");
	delete s->cameraStatus;
	s->cameraStatus = nullptr;
	if (s->source_ip) {
		free((void *)s->source_ip);
		s->source_ip = nullptr;
	}

	ssp_stop(s);
	bfree(s);
	ssp_blog(LOG_INFO, "source destroyed.");
}

struct obs_source_info create_ssp_source_info()
{
	struct obs_source_info ssp_source_info = {};
	ssp_source_info.id = "ssp_source";
	ssp_source_info.type = OBS_SOURCE_TYPE_INPUT;
	ssp_source_info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	ssp_source_info.get_name = ssp_source_getname;
	ssp_source_info.get_properties = ssp_source_getproperties;
	ssp_source_info.get_defaults = ssp_source_getdefaults;
	ssp_source_info.update = ssp_source_update;
	ssp_source_info.show = ssp_source_shown;
	ssp_source_info.hide = ssp_source_hidden;
	ssp_source_info.activate = ssp_source_activated;
	ssp_source_info.deactivate = ssp_source_deactivated;
	ssp_source_info.create = ssp_source_create;
	ssp_source_info.destroy = ssp_source_destroy;

	return ssp_source_info;
}
