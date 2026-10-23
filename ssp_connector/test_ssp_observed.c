/* Exercises the production parser with private real camera packet captures. */
#include "ssp.c"
#include <assert.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif

static const uint8_t *expected;
static size_t expected_len;
static unsigned calls;
static void video(const ssp_video_frame_t *f, void *u)
{
	(void)u;
	assert(f->data_len == expected_len);
	assert(memcmp(f->data, expected, expected_len) == 0);
	calls++;
}
static void audio(const ssp_audio_frame_t *f, void *u)
{
	(void)u;
	assert(f->data_len == expected_len);
	assert(memcmp(f->data, expected, expected_len) == 0);
	calls++;
}
static void captured(ssp_client_t *c, const char *path, bool is_video)
{
	FILE *f = fopen(path, "rb");
	assert(f);
	assert(fseek(f, 0, SEEK_END) == 0);
	long size = ftell(f);
	assert(size > 40 && size <= SSP_RECV_BUF_SIZE);
	rewind(f);
	uint8_t *p = malloc((size_t)size);
	assert(p && fread(p, 1, (size_t)size, f) == (size_t)size);
	fclose(f);
	size_t base = is_video ? 17 : 9;
	assert(p[0] == (is_video ? SSP_PKT_VIDEO : SSP_PKT_AUDIO));
	assert(read_be32(p + base) == 20 && read_be32(p + base + 4) == 16);
	expected = p + base + 20;
	expected_len = (size_t)size - base - 20;
	unsigned before = calls;
	if (is_video)
		handle_video(c, p, (uint32_t)size);
	else
		handle_audio(c, p, (uint32_t)size);
	assert(calls == before + 1);
	/* Truncated real extension must not reach a callback. */
	for (size_t n = base + 8; n <= base + 20; n++) {
		before = calls;
		if (is_video)
			handle_video(c, p, (uint32_t)n);
		else
			handle_audio(c, p, (uint32_t)n);
		assert(calls == before);
	}
	/* The same captured media remains accepted without the optional extension. */
	uint8_t *legacy = malloc(base + expected_len);
	assert(legacy);
	memcpy(legacy, p, base);
	memcpy(legacy + base, expected, expected_len);
	before = calls;
	if (is_video)
		handle_video(c, legacy, (uint32_t)(base + expected_len));
	else
		handle_audio(c, legacy, (uint32_t)(base + expected_len));
	assert(calls == before + 1);
	free(legacy);
	free(p);
}
int main(int argc, char **argv)
{
	assert(argc == 1 || argc == 3);
	ssp_client_t *c = ssp_client_create();
	assert(c);
	c->callbacks.on_video = video;
	c->callbacks.on_audio = audio;
#ifndef _WIN32
	int pair[2];
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
	c->sock = pair[0];
	atomic_store(&c->running, true);
	for (unsigned style = 0; style <= 2; style++) {
		assert(ssp_client_set_stream(c, style) == 0);
		assert(do_start_stream(c) == 0);
		uint8_t packet[13];
		assert(recv(pair[1], packet, sizeof(packet), MSG_WAITALL) == sizeof(packet));
		assert(read_be32(packet) == 9 && packet[4] == 0xca);
		assert(read_be32(packet + 5) == 1 && read_be32(packet + 9) == style);
	}
	close(pair[0]);
	close(pair[1]);
	c->sock = SSP_INVALID_SOCKET;
#endif
	if (argc == 3) {
		captured(c, argv[1], true);
		captured(c, argv[2], false);
	}
	ssp_client_destroy(c);
	puts(argc == 3 ? "PASS production selector and captured media extension parsing"
		       : "PASS production stream selector");
	return 0;
}
