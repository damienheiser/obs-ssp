#include "ffmpeg-decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int submit(struct ffmpeg_decode *d, unsigned char *data, int size, int width, int height, int ten_bit,
		  int *frames)
{
	struct obs_source_frame2 frame = {0};
	bool got = false;
	long long pts = *frames;
	if (!ffmpeg_decode_video(d, data, size, &pts, VIDEO_CS_709, VIDEO_RANGE_PARTIAL, &frame, &got))
		return 1;
	if (got) {
		if (frame.width != width || frame.height != height || !frame.data[0] ||
		    frame.linesize[0] < (uint32_t)width)
			return 2;
		if (ten_bit && frame.format != VIDEO_FORMAT_I010 && frame.format != VIDEO_FORMAT_P010)
			return 3;
		if (!ten_bit && frame.format != VIDEO_FORMAT_I420 && frame.format != VIDEO_FORMAT_NV12)
			return 4;
		++*frames;
	}
	return 0;
}
int main(int argc, char **argv)
{
	if (argc != 9)
		return 2;
	enum AVCodecID id = !strcmp(argv[1], "h264") ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;
	int width = atoi(argv[3]), height = atoi(argv[4]), expected = atoi(argv[5]), ten_bit = atoi(argv[6]),
	    hw = atoi(argv[7]);
	FILE *file = fopen(argv[2], "rb");
	if (!file)
		return 3;
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	rewind(file);
	unsigned char *input = calloc(1, length + AV_INPUT_BUFFER_PADDING_SIZE);
	if (!input)
		return 4;
	if (fread(input, 1, length, file) != (size_t)length)
		return 5;
	fclose(file);
	struct ffmpeg_decode d = {0};
	if (ffmpeg_decode_init(&d, id, hw) < 0)
		return 6;
	if (hw && !d.hw) {
		puts("hardware decoder unavailable");
		ffmpeg_decode_free(&d);
		free(input);
		return 77;
	}
	AVCodecParserContext *parser = av_parser_init(id);
	if (!parser)
		return 7;
	unsigned char *packet = NULL;
	int packet_size = 0, frames = 0, error = 0;
	size_t offset = 0;
	while (offset < (size_t)length) {
		int used = av_parser_parse2(parser, d.decoder, &packet, &packet_size, input + offset,
					    (int)(length - offset), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
		if (used < 0)
			return 8;
		offset += used;
		if (packet_size && (error = submit(&d, packet, packet_size, width, height, ten_bit, &frames)))
			return 10 + error;
		if (!used && !packet_size)
			return 15;
	}
	av_parser_parse2(parser, d.decoder, &packet, &packet_size, NULL, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
	if (packet_size && (error = submit(&d, packet, packet_size, width, height, ten_bit, &frames)))
		return 20 + error;
	for (int i = 0; i < expected + 32 && frames < expected; i++)
		if ((error = submit(&d, NULL, 0, width, height, ten_bit, &frames)))
			return 30 + error;
	printf("%s %dx%d %s-bit %s fps=%s frames=%d/%d\n", argv[1], width, height, ten_bit ? "10" : "8",
	       hw ? "hardware" : "software", argv[8], frames, expected);
	av_parser_close(parser);
	ffmpeg_decode_free(&d);
	free(input);
	return frames == expected ? 0 : 40;
}
