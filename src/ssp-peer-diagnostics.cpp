/* Opt-in diagnostics: actual OBS sources, encoders, transport and decoders. */
#include "ssp-peer-obs.h"
#include "obs-websocket-api.h"
#include <QCoreApplication>
#include <obs-module.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRandomGenerator>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <array>
#include <cmath>
#include <mutex>

namespace {
struct AudioMeasure {
	std::mutex mutex;
	uint64_t frames = 0, crossings = 0;
	double energy = 0;
	float previous = 0;
	static void capture(void *data, obs_source_t *, const audio_data *audio, bool)
	{
		auto s = static_cast<AudioMeasure *>(data);
		std::lock_guard<std::mutex> lock(s->mutex);
		const float *samples = (const float *)audio->data[0];
		if (!samples)
			return;
		for (uint32_t i = 0; i < audio->frames; ++i) {
			auto x = samples[i];
			if (s->previous <= 0 && x > 0)
				++s->crossings;
			s->previous = x;
			s->energy += x * x;
		}
		s->frames += audio->frames;
	}
	QJsonObject result()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return {{"samples", qint64(frames)},
			{"frequency_hz", frames ? crossings * 48000.0 / frames : 0},
			{"mean_square", frames ? energy / frames : 0}};
	}
};
struct Test : QObject {
	obs_source_t *source;
	QString state = "Ready: test uses private OBS views and generated tones, not camera media.";
	QTimer timer;
	bool running = false;
	unsigned ticks = 0;
	QString failure;
	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	audio_t *private_audio = nullptr;
	uint64_t sample_cursor = 0;
	obs_scene_t *scene = nullptr;
	obs_source_t *color = nullptr;
	std::array<obs_source_t *, 2> receivers{};
	obs_source_t *late = nullptr;
	std::array<obs_view_t *, 3> receive_views{};
	std::array<obs_encoder_t *, 2> video_encoders{}, audio_encoders{};
	std::array<AudioMeasure, 2> audio_measure;
	obs_service_t *service = nullptr;
	obs_output_t *output = nullptr;
	QByteArray key;
	uint16_t port = 0;
	obs_video_info info = {};
	explicit Test(obs_source_t *source) : source(source)
	{
		QObject::connect(&timer, &QTimer::timeout, this, [this] { tick(); });
	}
	~Test() { cleanup(); }
	void status(QString text)
	{
		state = std::move(text);
		obs_source_update_properties(source);
	}
	bool check(bool ok, const char *message)
	{
		if (!ok) {
			failure = QString::fromUtf8(message);
			finish();
		}
		return ok;
	}
	static bool tones_input(void *data, uint64_t start_ts, uint64_t, uint64_t *new_ts, uint32_t active_mixers,
				audio_output_data *mixes)
	{
		auto test = static_cast<Test *>(data);
		*new_ts = start_ts;
		for (unsigned mix = 0; mix < 2; ++mix)
			if (active_mixers & (1u << mix))
				for (unsigned channel = 0; channel < 2; ++channel) {
					auto samples = mixes[mix].data[channel];
					if (!samples)
						continue;
					for (unsigned i = 0; i < AUDIO_OUTPUT_FRAMES; ++i)
						samples[i] =
							(float)(0.3 * std::sin(2.0 * 3.141592653589793 *
									       (mix ? 880.0 : 440.0) *
									       (test->sample_cursor + i) / 48000.0));
				}
		test->sample_cursor += AUDIO_OUTPUT_FRAMES;
		return true;
	}
	void cleanup()
	{
		timer.stop();
		running = false;
		if (output) {
			obs_output_force_stop(output);
			obs_output_release(output);
			output = nullptr;
		}
		for (size_t i = 0; i < receivers.size(); ++i)
			if (receivers[i])
				obs_source_remove_audio_capture_callback(receivers[i], AudioMeasure::capture,
									 &audio_measure[i]);
		for (auto &v : receive_views)
			if (v) {
				obs_view_remove(v);
				obs_view_set_source(v, 0, nullptr);
				obs_view_destroy(v);
				v = nullptr;
			}
		if (late) {
			obs_source_release(late);
			late = nullptr;
		}
		for (auto &s : receivers)
			if (s) {
				obs_source_release(s);
				s = nullptr;
			}
		for (auto &e : video_encoders)
			if (e) {
				obs_encoder_release(e);
				e = nullptr;
			}
		for (auto &e : audio_encoders)
			if (e) {
				obs_encoder_release(e);
				e = nullptr;
			}
		if (private_audio) {
			audio_output_close(private_audio);
			private_audio = nullptr;
		}
		if (view) {
			obs_view_remove(view);
			obs_view_set_source(view, 0, nullptr);
			obs_view_destroy(view);
			view = nullptr;
			video = nullptr;
		}
		if (scene) {
			obs_scene_release(scene);
			scene = nullptr;
		}
		if (color) {
			obs_source_release(color);
			color = nullptr;
		}
		if (service) {
			obs_service_release(service);
			service = nullptr;
		}
		key.clear();
	}
	obs_source_t *receiver(int index, bool attach_audio)
	{
		auto settings = obs_data_create();
		obs_data_set_string(settings, "host", "127.0.0.1");
		obs_data_set_int(settings, "port", port);
		obs_data_set_string(settings, "key", key.constData());
		obs_data_set_int(settings, "video_track", index);
		obs_data_set_int(settings, "audio_track", index);
		obs_data_set_bool(settings, "hardware", false);
		auto receiver = obs_source_create_private(
			"ssp_peer_source", index ? "peer test receiver B" : "peer test receiver A", settings);
		obs_data_release(settings);
		if (receiver) {
			obs_source_set_audio_mixers(receiver, 0);
			if (attach_audio)
				obs_source_add_audio_capture_callback(receiver, AudioMeasure::capture,
								      &audio_measure[index]);
		}
		return receiver;
	}
	bool receiver_view(obs_source_t *receiver, size_t index)
	{
		auto view = obs_view_create();
		if (!view)
			return false;
		receive_views[index] = view;
		obs_view_set_source(view, 0, receiver);
		return obs_view_add2(view, &info) != nullptr;
	}
	void start()
	{
		if (running)
			return;
		cleanup();
		failure.clear();
		ticks = 0;
		for (auto &a : audio_measure) {
			std::lock_guard<std::mutex> lock(a.mutex);
			a.frames = a.crossings = 0;
			a.energy = 0;
			a.previous = 0;
		}
		if (!check(obs_get_video_info(&info), "OBS video is not initialized"))
			return;
		info.base_width = info.output_width = 320;
		info.base_height = info.output_height = 180;
		info.output_format = VIDEO_FORMAT_NV12;
		info.colorspace = VIDEO_CS_709;
		info.range = VIDEO_RANGE_PARTIAL;
		info.gpu_conversion = true;
		scene = obs_scene_create_private("peer test independent scene");
		auto settings = obs_data_create();
		obs_data_set_int(settings, "color", 0xff2050d0);
		obs_data_set_int(settings, "width", 320);
		obs_data_set_int(settings, "height", 180);
		const char *color_id = obs_get_latest_input_type_id("color_source");
		color = obs_source_create_private(color_id ? color_id : "color_source", "peer test color", settings);
		obs_data_release(settings);
		if (!check(scene && color, "Actual OBS color source unavailable"))
			return;
		obs_scene_add(scene, color);
		sample_cursor = 0;
		audio_output_info audio_info = {};
		audio_info.name = "peer private test tones";
		audio_info.samples_per_sec = 48000;
		audio_info.speakers = SPEAKERS_STEREO;
		audio_info.format = AUDIO_FORMAT_FLOAT_PLANAR;
		audio_info.input_callback = tones_input;
		audio_info.input_param = this;
		if (!check(audio_output_open(&private_audio, &audio_info) == AUDIO_OUTPUT_SUCCESS,
			   "Private actual OBS audio output unavailable"))
			return;
		view = obs_view_create();
		if (!check(view != nullptr, "Private video view unavailable"))
			return;
		obs_view_set_source(view, 0, obs_scene_get_source(scene));
		video = obs_view_add2(view, &info);
		if (!check(video != nullptr, "Private video renderer unavailable"))
			return;
		for (int i = 0; i < 2; ++i) {
			settings = obs_data_create();
			obs_data_set_int(settings, "bitrate", 600);
			obs_data_set_string(settings, "rate_control", "CBR");
			obs_data_set_string(settings, "preset", "ultrafast");
			obs_data_set_string(settings, "tune", "zerolatency");
			obs_data_set_int(settings, "keyint_sec", 1);
			obs_data_set_string(settings, "x264opts", "bframes=0");
			video_encoders[i] = obs_video_encoder_create(
				"obs_x264", i ? "peer video half-rate" : "peer video full-rate", settings, nullptr);
			obs_data_release(settings);
			if (!check(video_encoders[i] != nullptr, "Actual x264 encoder unavailable"))
				return;
			obs_encoder_set_video(video_encoders[i], video);
			if (i) {
				obs_encoder_set_scaled_size(video_encoders[i], 160, 90);
				if (!check(obs_encoder_set_frame_rate_divisor(video_encoders[i], 2),
					   "Encoder rate divisor unavailable"))
					return;
			}
			settings = obs_data_create();
			obs_data_set_int(settings, "bitrate", 128);
			audio_encoders[i] = obs_audio_encoder_create(
				"ffmpeg_aac", i ? "peer audio 880" : "peer audio 440", settings, i, nullptr);
			obs_data_release(settings);
			if (!check(audio_encoders[i] != nullptr, "Actual AAC encoder unavailable"))
				return;
			obs_encoder_set_audio(audio_encoders[i], private_audio);
		}
		QByteArray random(32, Qt::Uninitialized);
		for (int i = 0; i < 32; i += 4)
			qToBigEndian(QRandomGenerator::system()->generate(), random.data() + i);
		key = random.toHex();
		settings = obs_data_create();
		obs_data_set_string(settings, "bind", "127.0.0.1");
		obs_data_set_int(settings, "port", 0);
		obs_data_set_string(settings, "key", key.constData());
		service = obs_service_create("ssp_service", "peer diagnostic service", settings, nullptr);
		obs_data_release(settings);
		output = obs_output_create("ssp_output", "peer diagnostic output", nullptr, nullptr);
		if (!check(output && service, "SSP output/service registration missing"))
			return;
		obs_output_set_service(output, service);
		for (int i = 0; i < 2; ++i) {
			obs_output_set_video_encoder2(output, video_encoders[i], i);
			obs_output_set_audio_encoder(output, audio_encoders[i], i);
		}
		auto request = obs_data_create();
		obs_data_set_string(request, "outputName", "peer diagnostic output");
		auto response = obs_websocket_call_request("StartOutput", request);
		obs_data_release(request);
		bool started = response && response->status_code == 100;
		obs_websocket_request_response_free(response);
		if (!check(started, "obs-websocket StartOutput failed for test-owned output"))
			return;
		calldata_t status_data;
		calldata_init(&status_data);
		proc_handler_call(obs_output_get_proc_handler(output), "get_ssp_peer_status", &status_data);
		port = (uint16_t)calldata_int(&status_data, "port");
		calldata_free(&status_data);
		if (!check(port != 0, "Output did not report bound port"))
			return;
		for (int i = 0; i < 2; ++i) {
			receivers[i] = receiver(i, true);
			if (!check(receivers[i] && receiver_view(receivers[i], i), "Actual receiver view failed"))
				return;
		}
		running = true;
		status("Running actual OBS encoders and peer receivers");
		timer.start(500);
		blog(LOG_INFO,
		     "[obs-ssp] Peer self-test started: two video tracks, two distinct audio tones, loopback authenticated transport");
	}
	QJsonObject receiver_result(obs_source_t *receiver)
	{
		QJsonObject result;
		if (!receiver)
			return result;
		calldata_t data;
		calldata_init(&data);
		proc_handler_call(obs_source_get_proc_handler(receiver), "get_ssp_peer_status", &data);
		result["video_frames"] = qint64(calldata_int(&data, "video_frames"));
		result["audio_frames"] = qint64(calldata_int(&data, "audio_frames"));
		result["errors"] = qint64(calldata_int(&data, "errors"));
		result["width"] = (int)obs_source_get_width(receiver);
		result["height"] = (int)obs_source_get_height(receiver);
		calldata_free(&data);
		return result;
	}
	void tick()
	{
		if (++ticks == 5) {
			late = receiver(0, false);
			if (!check(late && receiver_view(late, 2), "Late join receiver creation failed"))
				return;
		}
		if (ticks >= 20)
			finish();
	}
	void finish()
	{
		timer.stop();
		QJsonArray results;
		bool passed = failure.isEmpty() && running;
		for (int i = 0; i < 2; ++i) {
			auto r = receiver_result(receivers[i]);
			auto a = audio_measure[i].result();
			r["audio"] = a;
			results.append(r);
			passed &= r["video_frames"].toInteger() > 20 && r["audio_frames"].toInteger() > 20 &&
				  r["errors"].toInteger() == 0 && r["width"].toInt() == (i ? 160 : 320) &&
				  r["height"].toInt() == (i ? 90 : 180);
			passed &= std::abs(a["frequency_hz"].toDouble() - (i ? 880 : 440)) < 15 &&
				  a["mean_square"].toDouble() > 0.0001;
		}
		auto joined = receiver_result(late);
		passed &= joined["video_frames"].toInteger() > 10 && joined["errors"].toInteger() == 0;
		QJsonObject report{
			{"passed", passed},    {"failure", failure},           {"receivers", results},
			{"late_join", joined}, {"fps_num", (int)info.fps_num}, {"fps_den", (int)info.fps_den}};
		if (output) {
			calldata_t data;
			calldata_init(&data);
			proc_handler_call(obs_output_get_proc_handler(output), "get_ssp_peer_status", &data);
			report["clients"] = (int)calldata_int(&data, "clients");
			auto manifest = QString::fromUtf8(calldata_string(&data, "manifest_json"));
			auto root = QJsonDocument::fromJson(manifest.toUtf8()).object();
			auto tracks = root["tracks"].toArray();
			QJsonArray safe;
			for (auto t : tracks) {
				auto item = t.toObject();
				item.remove("config");
				safe.append(item);
			}
			report["tracks"] = safe;
			bool cadence = true;
			for (auto value : safe) {
				auto track = value.toObject();
				if (track["kind"].toString() != "video")
					continue;
				auto divisor = track["index"].toInt() ? 2 : 1;
				cadence &= (int64_t)track["fps_num"].toInt() * info.fps_den * divisor ==
					   (int64_t)info.fps_num * track["fps_den"].toInt();
			}
			report["track_cadence_correct"] = cadence;
			passed &= cadence;

			calldata_free(&data);
		}
		auto request = obs_data_create();
		obs_data_set_string(request, "outputName", "peer diagnostic output");
		auto response = output ? obs_websocket_call_request("GetOutputStatus", request) : nullptr;
		bool ws_status = response && response->status_code == 100;
		obs_websocket_request_response_free(response);
		report["websocket_get_output_status"] = ws_status;
		passed &= ws_status;
		auto vendor_request = obs_data_create();
		obs_data_set_string(vendor_request, "vendorName", "obs-ssp");
		obs_data_set_string(vendor_request, "requestType", "GetTransportStatus");
		response = obs_websocket_call_request("CallVendorRequest", vendor_request);
		obs_data_release(vendor_request);
		bool ws_vendor = response && response->status_code == 100 && response->response_data &&
				 !QByteArray(response->response_data).contains(key);
		obs_websocket_request_response_free(response);
		report["websocket_vendor_status_without_key"] = ws_vendor;
		passed &= ws_vendor;
		response = output ? obs_websocket_call_request("StopOutput", request) : nullptr;
		obs_data_release(request);
		bool ws_stop = response && response->status_code == 100;
		obs_websocket_request_response_free(response);
		report["websocket_stop_output"] = ws_stop;
		passed &= ws_stop;
		report["passed"] = passed;
		auto path = QString("/Users/Shared/obs-ssp-peer-%1.json")
				    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
		QFile file(path);
		bool saved = file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
		if (saved) {
			file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
			file.write(QJsonDocument(report).toJson());
			file.close();
		}
		cleanup();
		status(QString(passed ? "PASS" : "FAIL") + ": " + (saved ? path : "could not save report") +
		       (failure.isEmpty() ? QString() : "; " + failure));
		blog(passed ? LOG_INFO : LOG_WARNING, "[obs-ssp] Peer self-test %s: report=%s",
		     passed ? "PASS" : "FAIL", path.toUtf8().constData());
	}
};
} // namespace
void register_ssp_peer_diagnostics()
{
	obs_source_info info = {};
	info.id = "ssp_peer_diagnostics";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = [](void *) {
		return "SSP Transport Acceptance Test (diagnostic)";
	};
	info.create = [](obs_data_t *, obs_source_t *source) -> void * {
		auto test = new Test(source);
		proc_handler_add(
			obs_source_get_proc_handler(source), "void run_peer_acceptance()",
			[](void *data, calldata_t *) { static_cast<Test *>(data)->start(); }, test);
		return test;
	};
	info.destroy = [](void *data) {
		delete static_cast<Test *>(data);
	};
	info.get_properties = [](void *data) {
		auto test = static_cast<Test *>(data);
		auto props = obs_properties_create();
		obs_properties_add_text(props, "status", test->state.toUtf8().constData(), OBS_TEXT_INFO);
		obs_properties_add_button2(
			props, "run", "Run real multitrack test",
			[](obs_properties_t *, obs_property_t *, void *data) {
				static_cast<Test *>(data)->start();
				return true;
			},
			data);
		return props;
	};
	obs_register_source(&info);
}

void run_ssp_peer_acceptance()
{
	auto source = obs_source_create_private("ssp_peer_diagnostics", "private peer acceptance", nullptr);
	if (!source) {
		blog(LOG_WARNING, "[obs-ssp] Could not create private peer acceptance test");
		return;
	}
	calldata_t data;
	calldata_init(&data);
	proc_handler_call(obs_source_get_proc_handler(source), "run_peer_acceptance", &data);
	calldata_free(&data);
	QTimer::singleShot(25000, QCoreApplication::instance(), [source] { obs_source_release(source); });
}
