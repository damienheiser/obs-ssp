#undef NDEBUG
#include "ssp-peer-plan.h"
#include "ssp-peer-timing.h"
#include <cassert>
#include <cstdio>
int main()
{
	assert(ssp_encoder_timestamp_us(1001, 30000) == 33367);
	assert(ssp_encoder_timestamp_us(30000, 30000) == 1000000);
	assert(ssp_encoder_timestamp_us(2002, 30000) == 66733);
	assert(ssp_encoder_timestamp_us(-1001, 30000) == -33367);
	assert(ssp_encoder_timestamp_us(1024, 48000) == 21333);
	auto settings = obs_data_create();
	SspTrackPlan plan;
	assert(ssp_read_track_plan(settings, plan) && plan.video_count == 1 && plan.audio_count == 1);
	obs_data_set_int(settings, "video_tracks", 2);
	obs_data_set_int(settings, "audio_tracks", 2);
	obs_data_set_int(settings, "video_scale_1", 2);
	obs_data_set_int(settings, "video_divisor_1", 2);
	obs_data_set_int(settings, "audio_mixer_1", 1);
	assert(ssp_read_track_plan(settings, plan));
	assert(plan.video[1].scale == 2 && plan.video[1].divisor == 2 && plan.audio[1].mixer == 1);
	obs_data_set_int(settings, "video_tracks", 11);
	assert(!ssp_read_track_plan(settings, plan));
	obs_data_set_int(settings, "video_tracks", 2);
	obs_data_set_int(settings, "audio_tracks", 7);
	assert(!ssp_read_track_plan(settings, plan));
	obs_data_set_int(settings, "audio_tracks", 2);
	obs_data_set_int(settings, "video_scale_1", 3);
	assert(!ssp_read_track_plan(settings, plan));
	obs_data_set_int(settings, "video_scale_1", 2);
	obs_data_set_int(settings, "video_divisor_1", 0);
	assert(!ssp_read_track_plan(settings, plan));
	obs_data_set_int(settings, "video_divisor_1", 2);
	obs_data_set_int(settings, "audio_mixer_1", 6);
	assert(!ssp_read_track_plan(settings, plan));
	obs_data_set_int(settings, "audio_mixer_1", 1);
	obs_data_set_int(settings, "video_bitrate_1", 0);
	assert(!ssp_read_track_plan(settings, plan));
	obs_data_set_int(settings, "video_bitrate_1", 2500);
	assert(ssp_read_track_plan(settings, plan));
	obs_data_release(settings);
	puts("PASS actual OBS settings validate multitrack bounds and independent mix/cadence plans");
}
