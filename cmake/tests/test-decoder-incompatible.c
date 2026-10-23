#include "ffmpeg-decode.h"
#include <stdio.h>
int main(void)
{
	struct ffmpeg_decode decoder = {0};
	if (ffmpeg_decode_compatible())
		return 2;
	if (ffmpeg_decode_init(&decoder, AV_CODEC_ID_AAC, false) >= 0 || decoder.decoder)
		return 3;
	puts("Actual incompatible FFmpeg major versions rejected before decoder allocation");
	return 0;
}
