#include "ffmpeg-decode.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	bool compatible = ffmpeg_decode_compatible();
	bool expected = (avcodec_version() >> 16) == LIBAVCODEC_VERSION_MAJOR &&
			(avutil_version() >> 16) == LIBAVUTIL_VERSION_MAJOR;
	if (compatible != expected)
		return 3;
	struct ffmpeg_decode d = {0};
	int result = ffmpeg_decode_init(&d, AV_CODEC_ID_AAC, false);
	if (!compatible) {
		if (result >= 0)
			return 4;
		fputs("unexpected incompatible FFmpeg runtime in decode test\n", stderr);
		return 11;
	}
	if (result < 0)
		return 5;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 6;
	unsigned char data[65536] = {0};
	size_t n = fread(data, 1, sizeof(data), f);
	fclose(f);
	if (n < 7)
		return 7;
	size_t size = ((data[3] & 3) << 11) | (data[4] << 3) | (data[5] >> 5);
	if (size > n)
		return 8;
	struct obs_source_audio audio = {0};
	bool got = false;
	if (!ffmpeg_decode_audio(&d, data, size, &audio, &got) || !got)
		return 9;
	printf("rate=%u frames=%u speakers=%d\n", audio.samples_per_sec, audio.frames, audio.speakers);
	if (audio.samples_per_sec != 48000 || audio.frames != 1024 || audio.speakers != SPEAKERS_STEREO)
		return 10;
	ffmpeg_decode_free(&d);
	return 0;
}
