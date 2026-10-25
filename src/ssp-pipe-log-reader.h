#pragma once
#include "util/pipe.h"
#include <thread>
#include <utility>
#ifndef _WIN32
#include <signal.h>
#endif
// The owner retains the process-pipe handle until after this guard is destroyed.
// Kill before joining: a child rejected mid-message may be blocked on stdout.
inline void ssp_terminate_process_pipe(os_process_pipe_t *pipe)
{
	if (!pipe)
		return;
#ifdef _WIN32
	os_process_pipe_signal(pipe, 0);
#else
	os_process_pipe_signal(pipe, SIGKILL);
#endif
}
class SspPipeLogReader {
	os_process_pipe_t *pipe;
	std::thread reader;

public:
	SspPipeLogReader(os_process_pipe_t *p, std::thread thread) : pipe(p), reader(std::move(thread)) {}
	~SspPipeLogReader()
	{
		ssp_terminate_process_pipe(pipe);
		if (reader.joinable())
			reader.join();
	}
};
