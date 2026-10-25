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

#ifndef OBS_SSP_VFRAMEQUEUE_H
#define OBS_SSP_VFRAMEQUEUE_H
#include <QQueue>
#include "../ssp_connector/include/ssp/limits.h"
#include <functional>
#include <QAtomicInt>
#include <QSemaphore>
#include <QMutex>
#include <imf/ISspClient.h>
#include "pthread.h"

class VFrameQueue {
	struct Frame {
		imf::SspH264Data data;
		uint64_t time;
		bool discontinuity;
	};
	using CallbackFunc = std::function<void(imf::SspH264Data *)>;

public:
	static constexpr size_t maxPacketBytes = SSP_MAX_PACKET_BYTES;
	static constexpr size_t maxResidentBytes = SSP_MAX_QUEUE_BYTES;
	static constexpr size_t maxResidentPackets = SSP_MAX_QUEUE_PACKETS;
	struct Stats {
		size_t residentBytes = 0, residentPackets = 0;
		uint64_t admitted = 0, dropped = 0, resets = 0;
		bool waitingKeyframe = false, running = false;
	};
	VFrameQueue();
	~VFrameQueue();
	bool enqueue(imf::SspH264Data, uint64_t time_us, bool keyframe);
	void setFrameTime(uint64_t time_us);
	void setFrameCallback(CallbackFunc);
	void setDiscontinuityCallback(std::function<void()>);
	Stats getStats() const;
	void start();
	void stop();

private:
	static void run(VFrameQueue *q);
	static void *pthread_run(void *q);
	void clearPendingLocked();
	CallbackFunc callback;
	std::function<void()> discontinuityCallback;
	QQueue<Frame> frameQueue;
	QSemaphore sem;
	mutable QMutex queueLock;
	QMutex lifecycleLock;
	pthread_t thread = {};
	QAtomicInt running{0};
	bool started = false, pendingDiscontinuity = false;
	uint64_t maxTime = 0;
	Stats stats;
};

#endif //OBS_SSP_VFRAMEQUEUE_H
