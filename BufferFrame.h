#pragma once
#include <vector>
using std::vector;

struct ModelVertex
{
	float    x, y, z;
	uint32_t abgr;
};

class BufferFrame
{
public:
	vector<ModelVertex> vertices;
	vector<uint32_t> indices;
};
