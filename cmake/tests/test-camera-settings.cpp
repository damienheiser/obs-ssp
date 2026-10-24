#undef NDEBUG
#include "ssp-settings.h"
#include <cassert>
#include <cstdio>
int main()
{
	const QStringList formats = {"C4KP29.97 (Low Noise)",
				     "C4KP23.98 (Low Noise)",
				     "C4KP24 (Low Noise)",
				     "C4KP25 (Low Noise)",
				     "C4KP59.94",
				     "1080P59.94"};
	const QStringList current = {"23.98", "24", "25", "29.97"};
	auto fps = ssp_camera_frame_rates(formats, "C4K (Low Noise)", current);
	assert(fps == QStringList({"29.97", "23.98", "24", "25"}));
	assert(!fps.contains("59.94"));
	assert(ssp_camera_frame_rates({}, "Future Camera Format", current) == current);
	assert(ssp_camera_resolution("4096*2160", true) == "C4K (Low Noise)");
	assert(ssp_camera_resolution("3840*2160", false) == "4K");
	assert(ssp_camera_resolution("C4K (Low Noise)", false) == "C4K (Low Noise)");
	assert(ssp_camera_encoder("h264") == "H.264");
	assert(ssp_camera_encoder("H264") == "H.264");
	assert(ssp_camera_encoder("H.265") == "H.265");
	assert(ssp_audio_timestamp_ns(true, 1234, 5000000000ULL, 0, 48000) == 5000000000ULL);
	assert(ssp_audio_timestamp_ns(false, 1234, 5000000000ULL, 0, 48000) == 1234000ULL);
	assert(ssp_audio_timestamp_ns(true, 1234, 5000000000ULL, 1024, 48000) == 5021333333ULL);
	assert(ssp_reported_camera_streams({"Stream0", "Stream1", "FutureStream"}) ==
	       QStringList({"Stream0", "Stream1"}));
	assert(ssp_reported_camera_streams({}).isEmpty());
	assert(ssp_reported_camera_streams({"Stream1"}) == QStringList({"Stream1"}));
	assert(ssp_camera_stream_selector(0) == 1);
	assert(ssp_camera_stream_selector(1) == 2);
	assert(ssp_camera_stream_selector(-1) == 0);
	assert(ssp_camera_stream_selector(99) == 0);
	puts("camera choices, stream selectors and audio timestamps passed");
}
