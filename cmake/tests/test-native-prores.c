#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../ssp_connector/ssp.c"
#include "ffmpeg-decode.h"
#include "ssp-video-codec.h"
struct state {
	struct ffmpeg_decode decoder;
	uint8_t *expected;
	size_t expected_size;
	uint64_t pts;
	int frames;
};
static unsigned char *load(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	assert(f);
	assert(fseek(f, 0, SEEK_END) == 0);
	long n = ftell(f);
	assert(n > 0 && n <= SSP_RECV_BUF_SIZE);
	rewind(f);
	unsigned char *data = malloc((size_t)n);
	assert(data);
	assert(fread(data, 1, (size_t)n, f) == (size_t)n);
	fclose(f);
	*size = (size_t)n;
	return data;
}
static void video(const ssp_video_frame_t *packet, void *userdata)
{
	struct state *s = userdata;
	assert(packet->data_len == s->expected_size && memcmp(packet->data, s->expected, s->expected_size) == 0);
	assert(packet->pts == s->pts);
	assert(ssp_detect_video_codec(1006, packet->data, packet->data_len) == AV_CODEC_ID_PRORES);
	bool got = false;
	long long pts = (long long)packet->pts;
	struct obs_source_frame2 frame = {0};
	assert(ffmpeg_decode_video(&s->decoder, (uint8_t *)packet->data, packet->data_len, &pts, VIDEO_CS_DEFAULT,
				   VIDEO_RANGE_DEFAULT, &frame, &got));
	if (!got) {
		fprintf(stderr, "FAIL native ProRes frame buffered instead of immediately displayed at index%d\n",
			s->frames);
		exit(20);
	}
	assert(frame.width == 4096 && frame.height == 2160 && frame.format == VIDEO_FORMAT_I210);
	assert(pts == (long long)s->pts);
	++s->frames;
}
int main(int argc, char **argv)
{
	assert(argc == 2);
	struct state s = {0};
	assert(ffmpeg_decode_init(&s.decoder, AV_CODEC_ID_PRORES, false) == 0);
	ssp_client_t client = {0};
	client.callbacks.on_video = video;
	client.callbacks.userdata = &s;
	for (int i = 0; i < 9; ++i) {
		char name[4096];
		snprintf(name, sizeof(name), "%s/video-%06d.payload", argv[1], i);
		s.expected = load(name, &s.expected_size);
		snprintf(name, sizeof(name), "%s/video-%06d.wire", argv[1], i);
		size_t size;
		unsigned char *wire = load(name, &size);
		assert(wire[0] == 0x6f && size == s.expected_size + 37);
		s.pts = read_be64(wire + 1);
		handle_video(&client, wire, (uint32_t)size);
		free(wire);
		free(s.expected);
	}
	assert(s.frames == 9);
	assert(ssp_detect_video_codec(1006, NULL, 0) == AV_CODEC_ID_PRORES);
	uint8_t invalid[8] = {0, 0, 0, 9, 'i', 'c', 'p', 'f'};
	assert(ssp_detect_video_codec(1006, invalid, sizeof(invalid)) == AV_CODEC_ID_NONE);
	assert(ssp_detect_video_codec(1006, invalid, 7) == AV_CODEC_ID_NONE);
	assert(!(s.decoder.decoder->thread_type & FF_THREAD_FRAME));
	assert(s.decoder.decoder->thread_count <= 4 && s.decoder.decoder->max_pixels == 33554432);
	ffmpeg_decode_free(&s.decoder);
	printf("PASS9actual nativeSSP ProRes frames -> immediateI2104096x2160, exactpayload/PTS, noEOFflush\n");
	return 0;
}
