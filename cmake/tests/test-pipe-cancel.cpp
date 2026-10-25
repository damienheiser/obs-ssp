#undef NDEBUG
#include "ssp-pipe-log-reader.h"
#include "../../ssp_connector/ssp_connector_proto.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--blocked-helper")) {
		Message bad = {};
		bad.type = VideoDataMsg;
		bad.length = SSP_MAX_IPC_BODY_BYTES + 1;
		fwrite(&bad, 1, sizeof(bad), stdout);
		fflush(stdout);
		unsigned char bytes[65536] = {};
		while (fwrite(bytes, 1, sizeof(bytes), stdout) == sizeof(bytes)) {
		}
		return 0;
	}
	auto args = os_process_args_create(argv[0]);
	os_process_args_add_arg(args, "--blocked-helper");
	auto pipe = os_process_pipe_create2(args, "r");
	os_process_args_destroy(args);
	assert(pipe);
	std::atomic<bool> finished{false};
	bool valid = true;
	size_t read = 0;
	auto start = std::chrono::steady_clock::now();
	{
		SspPipeLogReader guard(pipe, std::thread([&] {
					       uint8_t bytes[256];
					       while (os_process_pipe_read_err(pipe, bytes, sizeof(bytes))) {
					       }
					       finished = true;
				       }));
		Message header = {};
		while (read < sizeof(header)) {
			auto n = os_process_pipe_read(pipe, (uint8_t *)&header + read, sizeof(header) - read);
			if (!n)
				break;
			read += n;
		}
		valid = read == sizeof(header) && ssp_ipc_header_valid(&header);
	}
	os_process_pipe_destroy(pipe);
	assert(read == sizeof(Message) && !valid && finished &&
	       std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
	puts("PASS real malformed IPC child blocked on stdout is terminated before stderr join; no deadlock");
}
