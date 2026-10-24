#pragma once
#include <cstdint>
extern "C" {
#include <libavutil/mathematics.h>
}
// OBS encoder PTS/DTS are ticks at 1/timebase_den: timebase_num is already
// included in the tick increments. Match libobs packet_dts_usec conversion.
inline int64_t ssp_encoder_timestamp_us(int64_t ticks, int denominator)
{
	return av_rescale_q(ticks, AVRational{1, denominator}, AVRational{1, 1000000});
}
