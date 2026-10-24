#include "ssp-peer-obs.h"
#include "ssp-peer.h"
#include "ssp-peer-plan.h"
#include "obs-websocket-api.h"
#include "ffmpeg-decode.h"
#include <obs-module.h>
#include <util/platform.h>
#include <QJsonArray>
#include <QCoreApplication>
#include <QTimer>
#include <QJsonDocument>
#include <QUrl>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <numeric>
#include <vector>

namespace {
using ssp_peer::Track;
using ssp_peer::Packet;
static std::atomic<obs_websocket_vendor> vendor{nullptr};
static void state_event(bool active, uint32_t clients)
{
	auto registered = vendor.load();
	if (!registered)
		return;
	auto data = obs_data_create();
	obs_data_set_bool(data, "active", active);
	obs_data_set_int(data, "clients", clients);
	obs_websocket_vendor_emit_event(registered, "TransportStateChanged", data);
	obs_data_release(data);
}
struct Service {
	QByteArray url, key;
};
static void service_update(void *data, obs_data_t *settings)
{
	auto s = static_cast<Service *>(data);
	auto address = QString::fromUtf8(obs_data_get_string(settings, "bind"));
	auto port = obs_data_get_int(settings, "port");
	QUrl url;
	url.setScheme("ssp");
	url.setHost(address);
	url.setPort((int)port);
	s->url = url.toEncoded();
	s->key = QByteArray(obs_data_get_string(settings, "key"));
	if (s->key.isEmpty()) {
		s->key = ssp_peer::generate_key();
		obs_data_set_string(settings, "key", s->key.constData());
	}
}
static void defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "bind", "127.0.0.1");
	obs_data_set_default_int(settings, "port", 9998);
	obs_data_set_default_int(settings, "video_tracks", 1);
	obs_data_set_default_int(settings, "audio_tracks", 1);
	for (int i = 1; i < 10; ++i) {
		auto suffix = std::to_string(i);
		obs_data_set_default_int(settings, ("video_scale_" + suffix).c_str(), 2);
		obs_data_set_default_int(settings, ("video_divisor_" + suffix).c_str(), 1);
		obs_data_set_default_int(settings, ("video_bitrate_" + suffix).c_str(), 2500);
	}
	for (int i = 1; i < 6; ++i) {
		auto suffix = std::to_string(i);
		obs_data_set_default_int(settings, ("audio_mixer_" + suffix).c_str(), i);
		obs_data_set_default_int(settings, ("audio_bitrate_" + suffix).c_str(), 128);
	}
}
static bool track_visibility(obs_properties_t *p, obs_property_t *, obs_data_t *settings)
{
	for (int i = 1; i < 10; ++i)
		obs_property_set_visible(obs_properties_get(p, ("video_group_" + std::to_string(i)).c_str()),
					 i < obs_data_get_int(settings, "video_tracks"));
	for (int i = 1; i < 6; ++i)
		obs_property_set_visible(obs_properties_get(p, ("audio_group_" + std::to_string(i)).c_str()),
					 i < obs_data_get_int(settings, "audio_tracks"));
	return true;
}
static obs_properties_t *service_properties(void *)
{
	auto p = obs_properties_create();
	obs_properties_add_text(p, "bind", "Listen address", OBS_TEXT_DEFAULT);
	obs_properties_add_int(p, "port", "Listen port (0 selects an available port)", 0, 65535, 1);
	auto secret = obs_properties_add_text(p, "key", "Media key (256-bit)", OBS_TEXT_PASSWORD);
	obs_property_set_long_description(
		secret,
		"A new service generates a cryptographically random 256-bit key. Copy the same 64-hex-digit key to receivers. Only import keys from a secure generator. Media authentication/integrity is separate from obs-websocket; media is not encrypted, so use a trusted LAN or VPN.");
	auto count = obs_properties_add_int(p, "video_tracks", "Video tracks", 1, 10, 1);
	obs_property_set_modified_callback(count, track_visibility);
	count = obs_properties_add_int(p, "audio_tracks", "Audio tracks", 1, 6, 1);
	obs_property_set_modified_callback(count, track_visibility);
	for (int i = 1; i < 10; ++i) {
		auto suffix = std::to_string(i);
		auto group = obs_properties_create();
		auto scale = obs_properties_add_list(group, ("video_scale_" + suffix).c_str(),
						     "Resolution relative to primary", OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(scale, "Full size", 1);
		obs_property_list_add_int(scale, "Half size", 2);
		obs_property_list_add_int(scale, "Quarter size", 4);
		obs_properties_add_int(group, ("video_divisor_" + suffix).c_str(), "Frame rate divisor", 1, 4, 1);
		obs_properties_add_int(group, ("video_bitrate_" + suffix).c_str(), "Bitrate (kbps)", 128, 200000, 1);
		obs_properties_add_group(p, ("video_group_" + suffix).c_str(),
					 ("Video track " + suffix + " (primary codec)").c_str(), OBS_GROUP_NORMAL,
					 group);
	}
	for (int i = 1; i < 6; ++i) {
		auto suffix = std::to_string(i);
		auto group = obs_properties_create();
		obs_properties_add_int(group, ("audio_mixer_" + suffix).c_str(), "OBS audio mix (0–5)", 0, 5, 1);
		obs_properties_add_int(group, ("audio_bitrate_" + suffix).c_str(), "Bitrate (kbps)", 32, 1024, 1);
		obs_properties_add_group(p, ("audio_group_" + suffix).c_str(),
					 ("Audio track " + suffix + " (primary codec)").c_str(), OBS_GROUP_NORMAL,
					 group);
	}
	return p;
}
struct Output {
	obs_output_t *output;
	ssp_peer::Server server;
	struct Generated {
		bool video;
		size_t index;
		obs_encoder_t *encoder;
	};
	std::vector<Generated> generated;
	void clear_generated()
	{
		for (auto &g : generated) {
			if (g.video) {
				if (obs_output_get_video_encoder2(output, g.index) == g.encoder)
					obs_output_set_video_encoder2(output, nullptr, g.index);
			} else if (obs_output_get_audio_encoder(output, g.index) == g.encoder)
				obs_output_set_audio_encoder(output, nullptr, g.index);
			obs_encoder_release(g.encoder);
		}
		generated.clear();
	}
	std::mutex mutex;
	QVector<Track> tracks;
	std::array<std::atomic<uint64_t>, 16> track_packets{};
	std::atomic<bool> running{false}, failed{false};
	explicit Output(obs_output_t *o) : output(o) {}
};
static std::mutex output_registry_mutex;
static std::vector<Output *> outputs;
static QVector<Track> encoder_tracks(obs_output_t *output)
{
	QVector<Track> tracks;
	for (size_t i = 0; i < MAX_OUTPUT_VIDEO_ENCODERS; ++i) {
		auto encoder = obs_output_get_video_encoder2(output, i);
		if (!encoder)
			continue;
		Track t;
		t.index = (uint32_t)i;
		t.video = true;
		t.codec = QString::fromUtf8(obs_encoder_get_codec(encoder));
		t.width = obs_encoder_get_width(encoder);
		t.height = obs_encoder_get_height(encoder);
		auto info = video_output_get_info(obs_encoder_video(encoder));
		if (!info)
			return {};
		t.fps_num = info->fps_num;
		t.fps_den = info->fps_den; // obs_encoder_video already exposes the rate-divided video output.
		auto divisor = std::gcd(t.fps_num, t.fps_den);
		if (divisor) {
			t.fps_num /= divisor;
			t.fps_den /= divisor;
		}
		uint8_t *extra = nullptr;
		size_t size = 0;
		if (obs_encoder_get_extra_data(encoder, &extra, &size) && size) {
			if (size > ssp_peer::max_config)
				return {};
			t.extra = QByteArray((const char *)extra, (qsizetype)size);
		}
		tracks.push_back(t);
	}
	for (size_t i = 0; i < MAX_OUTPUT_AUDIO_ENCODERS; ++i) {
		auto encoder = obs_output_get_audio_encoder(output, i);
		if (!encoder)
			continue;
		Track t;
		t.index = (uint32_t)i;
		t.codec = QString::fromUtf8(obs_encoder_get_codec(encoder));
		t.sample_rate = obs_encoder_get_sample_rate(encoder);
		auto info = audio_output_get_info(obs_encoder_audio(encoder));
		if (!info)
			return {};
		t.channels = (uint32_t)get_audio_channels(info->speakers);
		uint8_t *extra = nullptr;
		size_t size = 0;
		if (obs_encoder_get_extra_data(encoder, &extra, &size) && size) {
			if (size > ssp_peer::max_config)
				return {};
			t.extra = QByteArray((const char *)extra, (qsizetype)size);
		}
		tracks.push_back(t);
	}
	return tracks;
}
static bool generate_tracks(Output *s, obs_data_t *settings)
{
	SspTrackPlan plan;
	if (!ssp_read_track_plan(settings, plan))
		return false;
	s->clear_generated();
	auto primary_video = obs_output_get_video_encoder2(s->output, 0);
	auto primary_audio = obs_output_get_audio_encoder(s->output, 0);
	if (!primary_video || !primary_audio)
		return false;
	for (int i = 1; i < plan.video_count; ++i) {
		if (obs_output_get_video_encoder2(s->output, i))
			continue;
		auto config = obs_encoder_get_settings(primary_video);
		auto copy = obs_data_create();
		obs_data_apply(copy, config);
		obs_data_release(config);
		obs_data_set_int(copy, "bitrate", plan.video[i].bitrate);
		auto name = std::string(obs_output_get_name(s->output)) + " video " + std::to_string(i);
		auto encoder = obs_video_encoder_create(obs_encoder_get_id(primary_video), name.c_str(), copy, nullptr);
		obs_data_release(copy);
		if (!encoder)
			return false;
		s->generated.push_back({true, (size_t)i, encoder});
		obs_encoder_set_video(encoder, obs_encoder_video(primary_video));
		auto width = obs_encoder_get_width(primary_video) / plan.video[i].scale;
		auto height = obs_encoder_get_height(primary_video) / plan.video[i].scale;
		if (width < 32 || height < 18)
			return false;
		obs_encoder_set_scaled_size(encoder, width & ~1u, height & ~1u);
		if (!obs_encoder_set_frame_rate_divisor(encoder, plan.video[i].divisor))
			return false;
		obs_output_set_video_encoder2(s->output, encoder, i);
	}
	for (int i = 1; i < plan.audio_count; ++i) {
		if (obs_output_get_audio_encoder(s->output, i))
			continue;
		auto config = obs_encoder_get_settings(primary_audio);
		auto copy = obs_data_create();
		obs_data_apply(copy, config);
		obs_data_release(config);
		obs_data_set_int(copy, "bitrate", plan.audio[i].bitrate);
		auto name = std::string(obs_output_get_name(s->output)) + " audio " + std::to_string(i);
		auto encoder = obs_audio_encoder_create(obs_encoder_get_id(primary_audio), name.c_str(), copy,
							plan.audio[i].mixer, nullptr);
		obs_data_release(copy);
		if (!encoder)
			return false;
		s->generated.push_back({false, (size_t)i, encoder});
		obs_encoder_set_audio(encoder, obs_encoder_audio(primary_audio));
		obs_output_set_audio_encoder(s->output, encoder, i);
	}
	return true;
}
static bool output_start(void *data)
{
	auto s = static_cast<Output *>(data);
	auto service = obs_output_get_service(s->output);
	if (!service || strcmp(obs_service_get_type(service), "ssp_service") != 0) {
		obs_output_set_last_error(s->output, "Select the SSP peer streaming service.");
		return false;
	}
	auto plan_settings = obs_service_get_settings(service);
	bool generated = generate_tracks(s, plan_settings);
	obs_data_release(plan_settings);
	if (!generated) {
		s->clear_generated();
		obs_output_set_last_error(
			s->output,
			"Invalid SSP track plan or unavailable encoder. Check track counts, scale, cadence and audio mixes.");
		return false;
	}
	if (!obs_output_can_begin_data_capture(s->output, 0) || !obs_output_initialize_encoders(s->output, 0))
		return false;
	auto tracks = encoder_tracks(s->output);
	if (ssp_peer::manifest_bytes(tracks).isEmpty()) {
		obs_output_set_last_error(s->output,
					  "Attached encoders have unsupported or incomplete track descriptions.");
		return false;
	}
	auto settings = obs_service_get_settings(service);
	QString bind = QString::fromUtf8(obs_data_get_string(settings, "bind"));
	auto port = obs_data_get_int(settings, "port");
	QByteArray key(obs_data_get_string(settings, "key"));
	obs_data_release(settings);
	if (port < 0 || port > 65535 || !s->server.start(bind, (uint16_t)port, key, tracks)) {
		obs_output_set_last_error(
			s->output,
			"Could not listen: check address/port and generate or import a 64-hex-digit media key.");
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(s->mutex);
		s->tracks = tracks;
	}
	for (auto &counter : s->track_packets)
		counter = 0;
	s->failed = false;
	s->running = true;
	if (!obs_output_begin_data_capture(s->output, 0)) {
		s->running = false;
		s->server.stop();
		return false;
	}
	state_event(true, 0);
	blog(LOG_INFO, "[obs-ssp] Peer output started: %lld advertised tracks, authenticated SSP v1",
	     (long long)tracks.size());
	return true;
}
static void output_stop(void *data, uint64_t)
{
	auto s = static_cast<Output *>(data);
	s->running = false;
	obs_output_end_data_capture(s->output);
	s->server.stop();
	state_event(false, 0);
	blog(LOG_INFO, "[obs-ssp] Peer output stopped");
}
static void output_failure(Output *s, int code, const char *reason)
{
	if (s->failed.exchange(true))
		return;
	s->running = false;
	obs_output_set_last_error(s->output, reason);
	s->server.stop();
	obs_output_signal_stop(s->output, code);
}
static void output_packet(void *data, encoder_packet *packet)
{
	auto s = static_cast<Output *>(data);
	if (!s->running || s->failed)
		return;
	if (!packet) {
		output_failure(s, OBS_OUTPUT_ENCODE_ERROR, "Encoder failed.");
		return;
	}
	if (packet->timebase_den <= 0 || packet->timebase_num <= 0 || packet->size > ssp_peer::max_packet - 62) {
		output_failure(s, OBS_OUTPUT_ERROR, "Encoder packet exceeds SSP bounds.");
		return;
	}
	Packet p;
	p.video = packet->type == OBS_ENCODER_VIDEO;
	p.index = (uint32_t)packet->track_idx;
	p.keyframe = packet->keyframe;
	p.pts_us = av_rescale_q(packet->pts, AVRational{packet->timebase_num, packet->timebase_den},
				AVRational{1, 1000000});
	p.dts_us = av_rescale_q(packet->dts, AVRational{packet->timebase_num, packet->timebase_den},
				AVRational{1, 1000000});
	p.data = QByteArray((const char *)packet->data, (qsizetype)packet->size);
	if (!s->server.publish(p)) {
		output_failure(s, OBS_OUTPUT_ERROR, "SSP sender queue or track bounds exceeded.");
		return;
	}
	auto index = p.video ? p.index : 10 + p.index;
	if (index < s->track_packets.size())
		++s->track_packets[index];
}
static void output_status(void *data, calldata_t *cd)
{
	auto s = static_cast<Output *>(data);
	auto status = s->server.status();
	calldata_set_int(cd, "port", s->server.port());
	calldata_set_int(cd, "clients", status.clients);
	calldata_set_int(cd, "packets", status.packets);
	calldata_set_bool(cd, "active", s->running && !s->failed);
	std::lock_guard<std::mutex> lock(s->mutex);
	auto bytes = ssp_peer::manifest_bytes(s->tracks);
	calldata_set_string(cd, "manifest_json", bytes.constData());
}
static void *output_create(obs_data_t *, obs_output_t *output)
{
	auto s = new Output(output);
	proc_handler_add(
		obs_output_get_proc_handler(output),
		"void get_ssp_peer_status(out int port, out int clients, out int packets, out bool active, out string manifest_json)",
		output_status, s);
	std::lock_guard<std::mutex> lock(output_registry_mutex);
	outputs.push_back(s);
	return s;
}
static void output_destroy(void *data)
{
	auto s = static_cast<Output *>(data);
	{
		std::lock_guard<std::mutex> lock(output_registry_mutex);
		outputs.erase(std::remove(outputs.begin(), outputs.end(), s), outputs.end());
	}
	s->server.stop();
	s->clear_generated();
	delete s;
}
static uint64_t output_bytes(void *data)
{
	return static_cast<Output *>(data)->server.status().bytes;
}
static float output_congestion(void *data)
{
	return static_cast<Output *>(data)->server.status().overflow ? 1.0f : 0.0f;
}
static enum AVCodecID codec_id(const QString &codec)
{
	if (codec == "h264")
		return AV_CODEC_ID_H264;
	if (codec == "hevc")
		return AV_CODEC_ID_HEVC;
	if (codec == "prores")
		return AV_CODEC_ID_PRORES;
	if (codec == "aac")
		return AV_CODEC_ID_AAC;
	if (codec == "pcm_s16le")
		return AV_CODEC_ID_PCM_S16LE;
	return AV_CODEC_ID_NONE;
}
struct Receiver {
	obs_source_t *source;
	ssp_peer::Client client;
	std::mutex mutex;
	QVector<Track> tracks;
	ffmpeg_decode video{}, audio{};
	int video_index = 0, audio_index = 0;
	bool hardware = true, decoder_error = false, clock_set = false;
	int64_t remote_base = 0;
	uint64_t local_base = 0, video_frames = 0, audio_frames = 0, errors = 0;
	QString state = "not connected";
	explicit Receiver(obs_source_t *source) : source(source) {}
	~Receiver()
	{
		client.stop();
		ffmpeg_decode_free(&video);
		ffmpeg_decode_free(&audio);
	}
	void status(const QString &value)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!decoder_error)
				state = value;
		}
		obs_source_update_properties(source);
	}
	void manifest(const QVector<Track> &values)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			tracks = values;
			bool found_video = video_index < 0, found_audio = audio_index < 0;
			for (const auto &track : tracks) {
				if ((track.video ? (int)track.index == video_index : (int)track.index == audio_index)) {
					auto decoder = track.video ? &video : &audio;
					if (ffmpeg_decode_init_ex(decoder, codec_id(track.codec),
								  track.video && hardware && track.codec != "prores",
								  track.sample_rate, track.channels,
								  (const uint8_t *)track.extra.constData(),
								  track.extra.size()) < 0) {
						decoder_error = true;
						state = "decoder initialization failed";
						++errors;
						continue;
					}
					if (track.video)
						found_video = true;
					else
						found_audio = true;
				}
			}
			if (!found_video || !found_audio) {
				decoder_error = true;
				state = "selected track unavailable";
				++errors;
			}
		}
		obs_source_update_properties(source);
	}
	bool timestamp(int64_t pts, int64_t dts, uint64_t &result)
	{
		auto now = os_gettime_ns();
		if (!clock_set) {
			remote_base = dts;
			local_base = now + 120000000;
			clock_set = true;
		}
		int64_t value = (int64_t)local_base + (pts - remote_base) * 1000;
		if (value < 0 || value < (int64_t)now - 10000000000LL || value > (int64_t)now + 10000000000LL)
			return false;
		result = (uint64_t)value;
		return true;
	}
	void packet(const Packet &packet)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (decoder_error ||
		    (packet.video ? (int)packet.index != video_index : (int)packet.index != audio_index))
			return;
		uint64_t timestamp_ns;
		if (!timestamp(packet.pts_us, packet.dts_us, timestamp_ns)) {
			++errors;
			return;
		}
		bool got = false;
		auto data = (uint8_t *)packet.data.constData();
		if (packet.video) {
			obs_source_frame2 frame = {};
			long long pts = packet.pts_us;
			if (!ffmpeg_decode_video(&video, data, packet.data.size(), &pts, VIDEO_CS_DEFAULT,
						 VIDEO_RANGE_DEFAULT, &frame, &got)) {
				++errors;
				return;
			}
			if (got) {
				if (!timestamp(pts, packet.dts_us, timestamp_ns)) {
					++errors;
					return;
				}
				frame.timestamp = timestamp_ns;
				obs_source_output_video2(source, &frame);
				++video_frames;
			}
		} else {
			obs_source_audio frame = {};
			if (!ffmpeg_decode_audio(&audio, data, packet.data.size(), &frame, &got)) {
				++errors;
				return;
			}
			if (got) {
				frame.timestamp = timestamp_ns;
				obs_source_output_audio(source, &frame);
				++audio_frames;
			}
		}
	}
};
static void receiver_update(void *data, obs_data_t *settings)
{
	auto s = static_cast<Receiver *>(data);
	s->client.stop();
	{
		std::lock_guard<std::mutex> lock(s->mutex);
		ffmpeg_decode_free(&s->video);
		ffmpeg_decode_free(&s->audio);
		s->tracks.clear();
		s->clock_set = false;
		s->decoder_error = false;
		s->video_frames = s->audio_frames = s->errors = 0;
		s->video_index = (int)obs_data_get_int(settings, "video_track");
		s->audio_index = (int)obs_data_get_int(settings, "audio_track");
		s->hardware = obs_data_get_bool(settings, "hardware");
	}
	QString host = QString::fromUtf8(obs_data_get_string(settings, "host"));
	auto port = obs_data_get_int(settings, "port");
	QByteArray key(obs_data_get_string(settings, "key"));
	ssp_peer::Callbacks callbacks;
	callbacks.state = [s](const QString &state) {
		s->status(state);
	};
	callbacks.manifest = [s](const QVector<Track> &tracks) {
		s->manifest(tracks);
	};
	callbacks.packet = [s](const Packet &packet) {
		s->packet(packet);
	};
	if (port < 1 || port > 65535) {
		s->status("invalid port");
		return;
	}
	s->client.start(host, (uint16_t)port, key, std::move(callbacks));
}
static obs_properties_t *receiver_properties(void *data)
{
	auto s = static_cast<Receiver *>(data);
	auto p = obs_properties_create();
	obs_properties_add_text(p, "host", "Sender host", OBS_TEXT_DEFAULT);
	obs_properties_add_int(p, "port", "SSP peer port", 1, 65535, 1);
	obs_properties_add_text(p, "key", "Media key (256-bit)", OBS_TEXT_PASSWORD);
	obs_properties_add_bool(p, "hardware", "Hardware decoding");
	auto video =
		obs_properties_add_list(p, "video_track", "Video track", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	auto audio =
		obs_properties_add_list(p, "audio_track", "Audio track", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(video, "No video", -1);
	obs_property_list_add_int(audio, "No audio", -1);
	QString status = "not connected";
	if (s) {
		std::lock_guard<std::mutex> lock(s->mutex);
		status = s->state;
		for (const auto &track : s->tracks) {
			QString label = QString("%1: %2").arg(track.index + 1).arg(track.codec.toUpper());
			if (track.video)
				label += QString(" %1x%2 %3/%4 fps")
						 .arg(track.width)
						 .arg(track.height)
						 .arg(track.fps_num)
						 .arg(track.fps_den);
			else
				label += QString(" %1 Hz, %2 channels").arg(track.sample_rate).arg(track.channels);
			obs_property_list_add_int(track.video ? video : audio, label.toUtf8().constData(), track.index);
		}
		status += QString("; decoded video %1, audio %2; errors %3")
				  .arg(s->video_frames)
				  .arg(s->audio_frames)
				  .arg(s->errors);
	}
	obs_properties_add_text(p, "status", status.toUtf8().constData(), OBS_TEXT_INFO);
	return p;
}
static void receiver_status(void *data, calldata_t *cd)
{
	auto s = static_cast<Receiver *>(data);
	std::lock_guard<std::mutex> lock(s->mutex);
	calldata_set_int(cd, "video_frames", s->video_frames);
	calldata_set_int(cd, "audio_frames", s->audio_frames);
	calldata_set_int(cd, "errors", s->errors);
	calldata_set_string(cd, "state", s->state.toUtf8().constData());
}
static void *receiver_create(obs_data_t *settings, obs_source_t *source)
{
	auto s = new Receiver(source);
	proc_handler_add(
		obs_source_get_proc_handler(source),
		"void get_ssp_peer_status(out int video_frames, out int audio_frames, out int errors, out string state)",
		receiver_status, s);
	receiver_update(s, settings);
	return s;
}
} // namespace
void register_ssp_peer()
{
	obs_service_info service = {};
	service.id = "ssp_service";
	service.get_name = [](void *) {
		return "SSP (OBS to OBS)";
	};
	service.create = [](obs_data_t *settings, obs_service_t *) -> void * {
		auto s = new Service;
		service_update(s, settings);
		return s;
	};
	service.destroy = [](void *data) {
		delete static_cast<Service *>(data);
	};
	service.update = service_update;
	service.get_defaults = defaults;
	service.get_properties = service_properties;
	service.get_url = [](void *data) {
		return static_cast<Service *>(data)->url.constData();
	};
	service.get_key = [](void *data) {
		return static_cast<Service *>(data)->key.constData();
	};
	service.get_password = service.get_key;
	service.get_output_type = [](void *) {
		return "ssp_output";
	};
	service.get_protocol = [](void *) {
		return "SSP";
	};
	service.get_supported_video_codecs = [](void *) -> const char ** {
		static const char *codecs[] = {"h264", "hevc", "prores", nullptr};
		return codecs;
	};
	service.get_supported_audio_codecs = [](void *) -> const char ** {
		static const char *codecs[] = {"aac", "pcm_s16le", nullptr};
		return codecs;
	};
	obs_register_service(&service);
	obs_output_info output = {};
	output.id = "ssp_output";
	output.get_name = [](void *) {
		return "SSP peer stream";
	};
	output.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE | OBS_OUTPUT_MULTI_TRACK_AV;
	output.protocols = "SSP";
	output.create = output_create;
	output.destroy = output_destroy;
	output.start = output_start;
	output.stop = output_stop;
	output.encoded_packet = output_packet;
	output.get_total_bytes = output_bytes;
	output.get_congestion = output_congestion;
	output.encoded_video_codecs = "h264;hevc;prores";
	output.encoded_audio_codecs = "aac;pcm_s16le";
	obs_register_output(&output);
	obs_source_info source = {};
	source.id = "ssp_peer_source";
	source.type = OBS_SOURCE_TYPE_INPUT;
	source.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
	source.get_name = [](void *) {
		return "SSP Receiver (OBS)";
	};
	source.create = receiver_create;
	source.destroy = [](void *data) {
		delete static_cast<Receiver *>(data);
	};
	source.update = receiver_update;
	source.get_properties = receiver_properties;
	source.get_defaults = [](obs_data_t *settings) {
		obs_data_set_default_string(settings, "host", "127.0.0.1");
		obs_data_set_default_int(settings, "port", 9998);
		obs_data_set_default_int(settings, "video_tracks", 1);
		obs_data_set_default_int(settings, "audio_tracks", 1);
		for (int i = 1; i < 10; ++i) {
			auto suffix = std::to_string(i);
			obs_data_set_default_int(settings, ("video_scale_" + suffix).c_str(), 2);
			obs_data_set_default_int(settings, ("video_divisor_" + suffix).c_str(), 1);
			obs_data_set_default_int(settings, ("video_bitrate_" + suffix).c_str(), 2500);
		}
		for (int i = 1; i < 6; ++i) {
			auto suffix = std::to_string(i);
			obs_data_set_default_int(settings, ("audio_mixer_" + suffix).c_str(), i);
			obs_data_set_default_int(settings, ("audio_bitrate_" + suffix).c_str(), 128);
		}
		obs_data_set_default_bool(settings, "hardware", true);
	};
	obs_register_source(&source);
}
void ssp_peer_post_load()
{
	if (vendor || obs_websocket_get_api_version() < 3)
		return;
	vendor = obs_websocket_register_vendor("obs-ssp");
	if (!vendor)
		return;
	obs_websocket_vendor_register_request(
		vendor, "GetCapabilities",
		[](obs_data_t *, obs_data_t *response, void *) {
			obs_data_set_int(response, "schema_version", 1);
			obs_data_set_int(response, "protocol_version", 1);
			obs_data_set_int(response, "max_video_tracks", 10);
			obs_data_set_int(response, "max_audio_tracks", 6);
			obs_data_set_bool(response, "camera_controls_on_peer", false);
			obs_data_set_bool(response, "media_encrypted", false);
			auto encoders = obs_data_array_create();
			const char *id = nullptr;
			for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
				QString codec = QString::fromUtf8(obs_get_encoder_codec(id));
				if (codec_id(codec) == AV_CODEC_ID_NONE)
					continue;
				auto value = obs_data_create();
				obs_data_set_string(value, "id", id);
				obs_data_set_string(value, "codec", codec.toUtf8().constData());
				obs_data_array_push_back(encoders, value);
				obs_data_release(value);
			}
			obs_data_set_array(response, "registered_encoders", encoders);
			obs_data_array_release(encoders);
		},
		nullptr);
	obs_websocket_vendor_register_request(
		vendor, "GetTransportStatus",
		[](obs_data_t *, obs_data_t *response, void *) {
			auto array = obs_data_array_create();
			std::lock_guard<std::mutex> registry(output_registry_mutex);
			for (auto output : outputs) {
				auto value = obs_data_create();
				auto stats = output->server.status();
				obs_data_set_string(value, "name", obs_output_get_name(output->output));
				obs_data_set_bool(value, "active", output->running && !output->failed);
				obs_data_set_int(value, "port", output->server.port());
				obs_data_set_int(value, "clients", stats.clients);
				obs_data_set_int(value, "transport_bytes_queued", stats.bytes);
				obs_data_set_int(value, "packets", stats.packets);
				obs_data_set_int(value, "rejected_connections", stats.rejected);
				obs_data_set_bool(value, "queue_overflow", stats.overflow);
				{
					std::lock_guard<std::mutex> lock(output->mutex);
					auto root = QJsonDocument::fromJson(ssp_peer::manifest_bytes(output->tracks))
							    .object();
					auto tracks = root["tracks"].toArray();
					QJsonArray safe;
					for (auto track : tracks) {
						auto item = track.toObject();
						item.remove("config");
						auto index = item["index"].toInt();
						auto kind = item["kind"].toString();
						item["packets"] = qint64(
							output->track_packets[kind == "video" ? index : 10 + index]
								.load());
						safe.append(item);
					}
					auto text = QJsonDocument(QJsonObject{{"schema_version", 1}, {"tracks", safe}})
							    .toJson(QJsonDocument::Compact);
					auto object = obs_data_create_from_json(text.constData());
					obs_data_set_obj(value, "capabilities", object);
					obs_data_release(object);
				}
				obs_data_array_push_back(array, value);
				obs_data_release(value);
			}
			obs_data_set_array(response, "outputs", array);
			obs_data_array_release(array);
		},
		nullptr);
	blog(LOG_INFO,
	     "[obs-ssp] Registered obs-websocket peer capability/status requests; standard stream controls use existing authentication");
#ifdef OBS_SSP_AUTORUN_DIAGNOSTICS
	extern void run_ssp_peer_acceptance();
	QTimer::singleShot(8000, QCoreApplication::instance(), [] { run_ssp_peer_acceptance(); });
#endif
}
void ssp_peer_unload()
{
	auto registered = vendor.exchange(nullptr);
	if (!registered)
		return;
	_ph = nullptr;
	if (obs_websocket_get_api_version() >= 3) {
		obs_websocket_vendor_unregister_request(registered, "GetCapabilities");
		obs_websocket_vendor_unregister_request(registered, "GetTransportStatus");
	}
}
