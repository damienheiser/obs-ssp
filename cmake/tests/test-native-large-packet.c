#undef NDEBUG
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include "../../ssp_connector/ssp.c"
struct input {
	int fd;
	uint8_t *bytes;
	size_t size;
	atomic_bool *stop;
};
static void *writer(void *arg)
{
	struct input *in = arg;
	size_t offset = 0;
	while (offset < in->size && !atomic_load(in->stop)) {
		size_t chunk = in->size - offset;
		if (chunk > 65536)
			chunk = 65536;
		ssize_t n = write(in->fd, in->bytes + offset, chunk);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			usleep(1000);
			continue;
		}
		if (n <= 0)
			break;
		offset += (size_t)n;
	}
	return NULL;
}
static void callback(const ssp_video_frame_t *frame, void *arg)
{
	struct input *in = arg;
	assert(frame->data_len == in->size && !memcmp(frame->data, in->bytes, in->size));
	assert(frame->pts == 1234567);
}
int main(int argc, char **argv)
{
	assert(argc == 2);
	signal(SIGPIPE, SIG_IGN);
	FILE *f = fopen(argv[1], "rb");
	assert(f);
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	rewind(f);
	assert(size > 4 * 1024 * 1024 && size < 32 * 1024 * 1024 - 37);
	uint32_t body = (uint32_t)size + 37;
	uint8_t *bytes = calloc(1, (size_t)body + 4);
	assert(bytes);
	write_be32(bytes, body);
	bytes[4] = 0x6f;
	write_be32(bytes + 5, 0);
	write_be32(bytes + 9, 1234567);
	write_be32(bytes + 13, 5);
	write_be32(bytes + 21, 20);
	write_be32(bytes + 25, 16);
	assert(fread(bytes + 41, 1, (size_t)size, f) == (size_t)size);
	fclose(f);
	int pair[2];
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
	ssp_client_t client = {0};
	client.sock = pair[0];
	atomic_init(&client.running, true);
	atomic_bool stopped;
	atomic_init(&stopped, false);
	assert(fcntl(pair[1], F_SETFL, fcntl(pair[1], F_GETFL, 0) | O_NONBLOCK) == 0);
	struct input in = {pair[1], bytes, (size_t)body + 4, &stopped};
	pthread_t thread;
	assert(pthread_create(&thread, NULL, writer, &in) == 0);
	uint32_t count = 0;
	uint8_t *packet = ssp_recv_packet(&client, &count);
	if (!packet) {
		atomic_store(&stopped, true);
		pthread_join(thread, NULL);
		close(pair[0]);
		close(pair[1]);
		free(bytes);
		fprintf(stderr, "FAIL receiver rejected actual >4MiB access unit at admission\n");
		return 20;
	}
	assert(count == body && !memcmp(packet, bytes + 4, body));
	pthread_join(thread, NULL);
	close(pair[0]);
	close(pair[1]);
	struct input expected = {0, bytes + 41, (size_t)size};
	client.callbacks.on_video = callback;
	client.callbacks.userdata = &expected;
	handle_video(&client, packet, count);
	free(packet);
	free(bytes);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
	client.sock = pair[0];
	uint8_t invalid[4];
	write_be32(invalid, SSP_RECV_BUF_SIZE + 1);
	assert(write(pair[1], invalid, 4) == 4);
	assert(ssp_recv_packet(&client, &count) == NULL);
	close(pair[0]);
	close(pair[1]);
	puts("PASS production socket admission/parser preserves real >4MiB payload and rejects oversized prefix before allocation (unit framing, not camera stream)");
	return 0;
}
