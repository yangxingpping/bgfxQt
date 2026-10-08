#pragma once
#include "BufferFrame.h"
#include <memory>

using std::make_unique;
using std::unique_ptr;

class BufferManager
{
public:
	BufferManager();
	~BufferManager();
	void initialize();
	void shutdown();

	static unique_ptr<BufferFrame> getFrame();
	static unique_ptr<BufferFrame> putFrame(unique_ptr<BufferFrame> frame);
};
