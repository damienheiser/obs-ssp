#undef NDEBUG
#include "VFrameQueue.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
using namespace std::chrono_literals;
struct Gate {
	std::mutex mutex;
	std::condition_variable ready;
	bool entered = false, released = false;
	std::vector<int64_t> events;
	void callback(imf::SspH264Data *frame)
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (frame->pts == 0) {
			entered = true;
			ready.notify_all();
			ready.wait(lock, [&] { return released; });
		}
		assert(frame->data[0] == 0x5a);
		events.push_back((int64_t)frame->pts);
		ready.notify_all();
	}
	void awaitFirst()
	{
		std::unique_lock<std::mutex> lock(mutex);
		assert(ready.wait_for(lock, 2s, [&] { return entered; }));
	}
	void release()
	{
		std::lock_guard<std::mutex> lock(mutex);
		released = true;
		ready.notify_all();
	}
	void awaitLast(int64_t value)
	{
		std::unique_lock<std::mutex> lock(mutex);
		assert(ready.wait_for(lock, 2s, [&] { return !events.empty() && events.back() == value; }));
	}
};
static bool submit(VFrameQueue &queue, std::vector<uint8_t> &bytes, uint64_t pts, bool key = false)
{
	imf::SspH264Data frame = {};
	frame.pts = pts;
	frame.data = bytes.data();
	frame.len = bytes.size();
	frame.type = key ? 5 : 1;
	return queue.enqueue(frame, pts, key);
}
static void overloaded(bool byteBound)
{
	Gate gate;
	VFrameQueue queue;
	queue.setFrameCallback([&](auto *frame) { gate.callback(frame); });
	queue.setDiscontinuityCallback([&] {
		std::lock_guard<std::mutex> lock(gate.mutex);
		gate.events.push_back(-1);
	});
	queue.start();
	std::vector<uint8_t> bytes(byteBound ? 16 * 1024 * 1024 : 1, 0x5a);
	assert(submit(queue, bytes, 0));
	gate.awaitFirst();
	size_t total = byteBound ? 4 : VFrameQueue::maxResidentPackets;
	for (size_t i = 1; i < total; ++i)
		assert(submit(queue, bytes, i));
	auto full = queue.getStats();
	assert(full.residentBytes == total * bytes.size() && full.residentPackets == total);
	assert(full.residentBytes <= VFrameQueue::maxResidentBytes &&
	       full.residentPackets <= VFrameQueue::maxResidentPackets);
	assert(!submit(queue, bytes, 50));
	auto waiting = queue.getStats();
	assert(waiting.waitingKeyframe && waiting.residentBytes == bytes.size() && waiting.residentPackets == 1);
	assert(!submit(queue, bytes, 51));
	assert(submit(queue, bytes, 99, true));
	bytes[0] = 0;
	auto recovering = queue.getStats();
	assert(!recovering.waitingKeyframe && recovering.residentBytes == 2 * bytes.size());
	gate.release();
	gate.awaitLast(99);
	queue.stop();
	assert((gate.events == std::vector<int64_t>{0, -1, 99}));
	auto done = queue.getStats();
	assert(done.residentBytes == 0 && done.residentPackets == 0 && done.resets == 1 && done.dropped == total + 1);
}
static void stopped()
{
	Gate gate;
	VFrameQueue queue;
	queue.setFrameCallback([&](auto *frame) { gate.callback(frame); });
	std::vector<uint8_t> bytes(1, 0x5a);
	queue.start();
	assert(submit(queue, bytes, 0));
	gate.awaitFirst();
	assert(submit(queue, bytes, 1));
	std::thread stop([&] { queue.stop(); });
	auto deadline = std::chrono::steady_clock::now() + 2s;
	while (queue.getStats().running && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(1ms);
	assert(!queue.getStats().running && !submit(queue, bytes, 2));
	gate.release();
	stop.join();
	assert(queue.getStats().residentBytes == 0 && queue.getStats().residentPackets == 0);
	queue.stop();
	queue.start();
	assert(submit(queue, bytes, 42));
	gate.awaitLast(42);
	queue.stop();
	assert((gate.events == std::vector<int64_t>{0, 42}));
}
static void invalid_packet()
{
	Gate gate;
	VFrameQueue queue;
	queue.setFrameCallback([&](auto *frame) { gate.callback(frame); });
	queue.start();
	std::vector<uint8_t> huge(VFrameQueue::maxPacketBytes + 1, 0x5a);
	assert(!submit(queue, huge, 1, true));
	imf::SspH264Data empty = {};
	assert(!queue.enqueue(empty, 0, true));
	auto stats = queue.getStats();
	assert(stats.waitingKeyframe && stats.residentBytes == 0 && stats.residentPackets == 0);
	std::vector<uint8_t> small(1, 0x5a);
	assert(!submit(queue, small, 2));
	assert(submit(queue, small, 99, true));
	gate.awaitLast(99);
	queue.stop();
	assert(queue.getStats().residentBytes == 0 && queue.getStats().resets == 1);
}
int main()
{
	overloaded(true);
	overloaded(false);
	stopped();
	invalid_packet();
	puts("PASS bounded bytes/packets including in-flight, reference drop/keyframe reset order, copied ownership and stop/drain/restart");
}
