#include "VFrameQueue.h"
#include <condition_variable>
#include <mutex>
#include <vector>
#include <chrono>
#include <cstdio>
int main()
{
	VFrameQueue queue;
	std::mutex mutex;
	std::condition_variable condition;
	std::vector<uint64_t> actual;
	queue.setFrameCallback([&](imf::SspH264Data *frame) {
		std::lock_guard<std::mutex> lock(mutex);
		actual.push_back(frame->pts);
		condition.notify_one();
	});
	queue.start();
	for (uint64_t pts : {100ULL, 80ULL, 120ULL}) {
		imf::SspH264Data frame = {};
		frame.pts = pts;
		frame.type = 1;
		frame.data = reinterpret_cast<uint8_t *>(&frame.pts);
		frame.len = sizeof(frame.pts);
		queue.enqueue(frame, pts, false);
	}
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait_for(lock, std::chrono::seconds(2), [&]() { return actual.size() == 3; });
	}
	queue.stop();
	if (actual != std::vector<uint64_t>({100, 80, 120})) {
		fprintf(stderr, "Encoded packets with reordered PTS were lost\n");
		return 1;
	}
	puts("Encoded packet order preserved");
}
