#include "ffmpeg-decode.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 3;
	unsigned char data[4096];
	size_t size = fread(data, 1, sizeof(data), f);
	fclose(f);
	struct ffmpeg_decode d = {0};
	if (ffmpeg_decode_audio_init(&d, AV_CODEC_ID_PCM_S16LE, 48000, 2) < 0)
		return 4;
	struct obs_source_audio audio = {0};
	bool got = false;
	if (!ffmpeg_decode_audio(&d, data, size, &audio, &got) || !got)
		return 5;
	printf("PCM rate=%u frames=%u format=%d channels=%d\n", audio.samples_per_sec, audio.frames, audio.format,
	       audio.speakers);
	if (audio.samples_per_sec != 48000 || audio.frames != size / 4 || audio.format != AUDIO_FORMAT_16BIT ||
	    audio.speakers != SPEAKERS_STEREO)
		return 6;
	ffmpeg_decode_free(&d);
	return 0;
}
