#include "ffmpeg-decode.h"
#include "ssp-video-codec.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 3;
	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	rewind(f);
	unsigned char *data = malloc(length);
	if (!data)
		return 4;
	if (fread(data, 1, length, f) != (size_t)length)
		return 5;
	fclose(f);
	struct ffmpeg_decode d = {0};
	int frames = 0;
	size_t offset = 0;
	if (ffmpeg_decode_init(&d, AV_CODEC_ID_PRORES, false) < 0)
		return 6;
	while (offset < (size_t)length) {
		if ((size_t)length - offset < 8)
			return 7;
		uint32_t size = ((uint32_t)data[offset] << 24) | ((uint32_t)data[offset + 1] << 16) |
				((uint32_t)data[offset + 2] << 8) | data[offset + 3];
		if (size < 8 || size > (size_t)length - offset)
			return 8;
		if (ssp_detect_video_codec(0, data + offset, size) != AV_CODEC_ID_PRORES)
			return 9;
		bool got = false;
		long long pts = frames;
		struct obs_source_frame2 frame = {0};
		if (!ffmpeg_decode_video(&d, data + offset, size, &pts, VIDEO_CS_709, VIDEO_RANGE_PARTIAL, &frame,
					 &got))
			return 10;
		if (got) {
			if (frame.format != VIDEO_FORMAT_I210 || frame.width != 320 || frame.height != 180)
				return 11;
			++frames;
		}
		offset += size;
	}
	for (int i = 0; i < 64 && frames < 4; i++) {
		bool got = false;
		long long pts = 0;
		struct obs_source_frame2 frame = {0};
		if (!ffmpeg_decode_video(&d, NULL, 0, &pts, VIDEO_CS_709, VIDEO_RANGE_PARTIAL, &frame, &got))
			return 12;
		if (got) {
			if (frame.format != VIDEO_FORMAT_I210 || frame.width != 320 || frame.height != 180)
				return 13;
			++frames;
		}
	}
	printf("ProRes10-bit422 frames=%d/4\n", frames);
	ffmpeg_decode_free(&d);
	free(data);
	return frames == 4 ? 0 : 14;
}
