#include "BufferManager.h"
#include <concurrentqueue/moodycamel/concurrentqueue.h>

static moodycamel::ConcurrentQueue<unique_ptr<BufferFrame>> sBufferPool;

BufferManager::BufferManager()
{
}

BufferManager::~BufferManager()
{

}

unique_ptr<BufferFrame> BufferManager::getFrame()
{
	unique_ptr<BufferFrame> ret;
	sBufferPool.try_dequeue(ret);
	return std::move(ret);
}

void BufferManager::putFrame(unique_ptr<BufferFrame> frame)
{
	sBufferPool.enqueue(std::move(frame));
}

