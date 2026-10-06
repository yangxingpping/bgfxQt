#pragma once
#include <manifold/manifold.h>

class MachineCut
{
public:
	MachineCut() = default;
	~MachineCut() = default;
	
	static manifold::MeshGL TestCut();
};

