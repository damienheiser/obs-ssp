#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "ffmpeg-decode.h"
#include "ssp-video-codec.h"
int main(int argc, char **argv)
{
	assert(argc == 2);
	FILE *f = fopen(argv[1], "rb");
	assert(f);
	assert(fseek(f, 0, SEEK_END) == 0);
	long size = ftell(f);
	assert(size > 0 && size <= 32 * 1024 * 1024);
	rewind(f);
	uint8_t *data = malloc((size_t)size);
	assert(data);
	assert(fread(data, 1, (size_t)size, f) == (size_t)size);
	fclose(f);
	assert(ssp_detect_video_codec(1006, data, (size_t)size) == AV_CODEC_ID_PRORES);
	struct ffmpeg_decode decoder = {0};
	assert(ffmpeg_decode_prores_init(&decoder, 0, 12) < 0);
	assert(ffmpeg_decode_prores_init(&decoder, MKTAG('a', 'p', '4', 'h'), 10) < 0);
	assert(ffmpeg_decode_prores_init(&decoder, MKTAG('a', 'p', 'c', 'n'), 12) < 0);
	assert(ffmpeg_decode_prores_init(&decoder, MKTAG('b', 'a', 'd', '!'), 12) < 0);
	assert(ffmpeg_decode_prores_init(&decoder, MKTAG('a', 'p', '4', 'h'), 12) == 0);
	struct obs_source_frame2 frame = {0};
	bool got = false;
	long long pts = 1234567;
	bool decoded = ffmpeg_decode_video(&decoder, data, (size_t)size, &pts, VIDEO_CS_DEFAULT, VIDEO_RANGE_DEFAULT,
					   &frame, &got);
	if (!decoded)
		fprintf(stderr, "Decoder boundary failure: AVformat=%d OBSformat=%d decodedDimensions=%dx%d\n",
			decoder.frame ? decoder.frame->format : -1, frame.format,
			decoder.frame ? decoder.frame->width : 0, decoder.frame ? decoder.frame->height : 0);
	assert(decoded);
	assert(got && pts == 1234567 && frame.width == 4096 && frame.height == 2160 &&
	       frame.format == VIDEO_FORMAT_I412);
	assert(!frame.data[3]);
	assert(decoder.frame->format == AV_PIX_FMT_YUV444P12LE && decoder.decoder->bits_per_raw_sample == 12);
	for (int plane = 0; plane < 3; ++plane) {
		assert(frame.data[plane] && frame.linesize[plane] >= 4096 * 2);
		for (unsigned row = 0; row < 2160; ++row) {
			const uint16_t *pixels = (const uint16_t *)(frame.data[plane] + row * frame.linesize[plane]);
			for (unsigned column = 0; column < 4096; ++column)
				assert(pixels[column] <= 4095);
		}
	}
	puts("PASS actual ap4h C4K -> immediate I412, three full-resolution right-aligned12bit planes; no alpha/color qualification");
	ffmpeg_decode_free(&decoder);
	free(data);
	return 0;
}
