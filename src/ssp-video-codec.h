#pragma once
#include <libavcodec/codec_id.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline enum AVCodecID ssp_detect_video_codec(uint32_t encoder, const uint8_t *data, size_t size)
{
	if (data && size >= 8 && size <= 32u * 1024u * 1024u && memcmp(data + 4, "icpf", 4) == 0) {
		uint32_t length = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
				  data[3];
		if (length == size)
			return AV_CODEC_ID_PRORES;
	}
	if (encoder == 1006)
		return !data && !size ? AV_CODEC_ID_PRORES : AV_CODEC_ID_NONE;
	if (encoder == 96)
		return AV_CODEC_ID_H264;
	if (encoder == 265)
		return AV_CODEC_ID_HEVC;
	return AV_CODEC_ID_NONE;
}
