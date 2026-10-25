/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#include <util/platform.h>
#include "VFrameQueue.h"
#include <QDebug>

VFrameQueue::VFrameQueue() = default;
VFrameQueue::~VFrameQueue()
{
	stop();
}
void VFrameQueue::clearPendingLocked()
{
	while (!frameQueue.empty()) {
		auto frame = frameQueue.dequeue();
		stats.residentBytes -= frame.data.len;
		--stats.residentPackets;
		++stats.dropped;
		free(frame.data.data);
	}
	// The worker can have reserved one token before acquiring queueLock. Drain
	// unreserved tokens so repeated overload never creates an unbounded empty loop.
	sem.tryAcquire(sem.available());
}
void VFrameQueue::start()
{
	QMutexLocker lifecycle(&lifecycleLock);
	if (started)
		return;
	QMutexLocker lock(&queueLock);
	clearPendingLocked();
	stats = {};
	pendingDiscontinuity = false;
	running = 1;
	int error = pthread_create(&thread, nullptr, pthread_run, this);
	if (error) {
		running = 0;
		qWarning("[obs-ssp] Video queue thread creation failed: %d", error);
		return;
	}
	started = true;
}
void VFrameQueue::stop()
{
	QMutexLocker lifecycle(&lifecycleLock);
	if (!started)
		return;
	{
		QMutexLocker lock(&queueLock);
		running = 0;
	}
	sem.release();
	pthread_join(thread, nullptr);
	QMutexLocker lock(&queueLock);
	clearPendingLocked();
	stats.waitingKeyframe = false;
	pendingDiscontinuity = false;
	started = false;
}
void VFrameQueue::setFrameCallback(CallbackFunc cb)
{
	QMutexLocker lock(&queueLock);
	callback = std::move(cb);
}
void VFrameQueue::setDiscontinuityCallback(std::function<void()> cb)
{
	QMutexLocker lock(&queueLock);
	discontinuityCallback = std::move(cb);
}
void VFrameQueue::setFrameTime(uint64_t time_us)
{
	QMutexLocker lock(&queueLock);
	maxTime = time_us;
}
VFrameQueue::Stats VFrameQueue::getStats() const
{
	QMutexLocker lock(&queueLock);
	auto result = stats;
	result.running = running.loadAcquire() != 0;
	return result;
}
bool VFrameQueue::enqueue(imf::SspH264Data data, uint64_t time_us, bool keyframe)
{
	QMutexLocker lock(&queueLock);
	if (!running.loadAcquire()) {
		++stats.dropped;
		return false;
	}
	if (!data.data || !data.len || (size_t)data.len > maxPacketBytes) {
		clearPendingLocked();
		stats.waitingKeyframe = true;
		pendingDiscontinuity = true;
		++stats.dropped;
		return false;
	}
	if (stats.waitingKeyframe && !keyframe) {
		++stats.dropped;
		return false;
	}
	if (stats.residentPackets >= maxResidentPackets || (size_t)data.len > maxResidentBytes - stats.residentBytes) {
		clearPendingLocked();
		stats.waitingKeyframe = true;
		pendingDiscontinuity = true;
		if (!keyframe) {
			++stats.dropped;
			return false;
		}
	}
	// At most one 32MiB access unit is in flight after clearing pending packets.
	if (stats.residentPackets >= maxResidentPackets || (size_t)data.len > maxResidentBytes - stats.residentBytes) {
		++stats.dropped;
		return false;
	}
	auto copy = (uint8_t *)malloc(data.len);
	if (!copy) {
		clearPendingLocked();
		stats.waitingKeyframe = true;
		pendingDiscontinuity = true;
		++stats.dropped;
		return false;
	}
	memcpy(copy, data.data, data.len);
	data.data = copy;
	bool reset = stats.waitingKeyframe && keyframe && pendingDiscontinuity;
	if (reset) {
		stats.waitingKeyframe = false;
		pendingDiscontinuity = false;
	}
	frameQueue.enqueue({data, time_us, reset});
	stats.residentBytes += data.len;
	++stats.residentPackets;
	++stats.admitted;
	sem.release();
	return true;
}
void *VFrameQueue::pthread_run(void *q)
{
	run((VFrameQueue *)q);
	return nullptr;
}
void VFrameQueue::run(VFrameQueue *q)
{
	while (q->running.loadAcquire()) {
		q->sem.acquire();
		q->queueLock.lock();
		if (!q->running.loadAcquire()) {
			q->queueLock.unlock();
			break;
		}
		if (q->frameQueue.empty()) {
			q->queueLock.unlock();
			continue;
		}
		Frame current = q->frameQueue.dequeue();
		auto cb = q->callback;
		auto reset = q->discontinuityCallback;
		if (current.discontinuity)
			++q->stats.resets;
		q->queueLock.unlock();
		if (current.discontinuity && reset)
			reset();
		if (cb)
			cb(&current.data);
		free(current.data.data);
		QMutexLocker lock(&q->queueLock);
		q->stats.residentBytes -= current.data.len;
		--q->stats.residentPackets;
	}
}
