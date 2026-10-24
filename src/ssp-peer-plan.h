#pragma once
#include <obs.h>
#include <array>
#include <string>
struct SspVideoTrackConfig {
	uint32_t scale = 2, divisor = 1, bitrate = 2500;
};
struct SspAudioTrackConfig {
	uint32_t mixer = 0, bitrate = 128;
};
struct SspTrackPlan {
	int video_count = 1, audio_count = 1;
	std::array<SspVideoTrackConfig, 10> video;
	std::array<SspAudioTrackConfig, 6> audio;
};
inline bool ssp_read_track_plan(obs_data_t *settings, SspTrackPlan &plan)
{
	plan = {};
	auto value = [&](const std::string &key, long long fallback) {
		return obs_data_has_user_value(settings, key.c_str()) ||
				       obs_data_has_default_value(settings, key.c_str())
			       ? obs_data_get_int(settings, key.c_str())
			       : fallback;
	};
	auto videos = value("video_tracks", 1), audios = value("audio_tracks", 1);
	if (videos < 1 || videos > 10 || audios < 1 || audios > 6)
		return false;
	plan.video_count = (int)videos;
	plan.audio_count = (int)audios;
	for (int i = 1; i < videos; ++i) {
		auto suffix = std::to_string(i);
		auto scale = value("video_scale_" + suffix, 2), divisor = value("video_divisor_" + suffix, 1),
		     bitrate = value("video_bitrate_" + suffix, 2500);
		if ((scale != 1 && scale != 2 && scale != 4) || divisor < 1 || divisor > 4 || bitrate < 128 ||
		    bitrate > 200000)
			return false;
		plan.video[i] = {(uint32_t)scale, (uint32_t)divisor, (uint32_t)bitrate};
	}
	for (int i = 1; i < audios; ++i) {
		auto suffix = std::to_string(i);
		auto mixer = value("audio_mixer_" + suffix, i), bitrate = value("audio_bitrate_" + suffix, 128);
		if (mixer < 0 || mixer > 5 || bitrate < 32 || bitrate > 1024)
			return false;
		plan.audio[i] = {(uint32_t)mixer, (uint32_t)bitrate};
	}
	return true;
}
