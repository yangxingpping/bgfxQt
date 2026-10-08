#include "BufferManager.h"
#include <concurrentqueue/moodycamel/concurrentqueue.h>

#include <mutex>
using std::lock_guard;
using std::mutex;

static moodycamel::ConcurrentQueue<unique_ptr<BufferFrame>> sBufferPool;

unique_ptr<BufferFrame> s_bufferFrame = nullptr;
mutex s_bufferFrameMutex;

BufferManager::BufferManager()
{
}

BufferManager::~BufferManager()
{

}

unique_ptr<BufferFrame> BufferManager::getFrame()
{
	lock_guard<mutex> lock(s_bufferFrameMutex);
	unique_ptr<BufferFrame> ret = std::move(s_bufferFrame);
	//sBufferPool.try_dequeue(ret);
	return std::move(ret);
}

void BufferManager::putFrame(unique_ptr<BufferFrame> frame)
{
	lock_guard<mutex> lock(s_bufferFrameMutex);
	s_bufferFrame = std::move(frame);
	//sBufferPool.enqueue(std::move(frame));
}

