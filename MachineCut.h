#pragma once
#include <manifold/manifold.h>
#include <string>

using std::string;

class MachineCut
{
public:
	MachineCut() = default;
	~MachineCut() = default;
	
	static manifold::MeshGL TestCut();
	static manifold::MeshGL LoadMesh(const string& filename);
	static void ManifoldTest();
};

