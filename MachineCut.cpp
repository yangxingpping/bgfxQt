
#include "machinecut.h"
#include "wlog.h"
#include <vector>
using std::vector;
using namespace manifold;

void manifold_tool_combine(std::vector<Manifold>& tools, Manifold& result) {
	WLOG_FUNCTION_TIMER();
	if (tools.empty()) {
		spdlog::warn("No tools to combine.");
		return;
	}
	// 使用 BatchBoolean 进行批量布尔运算
	result = Manifold::BatchBoolean(tools, OpType::Add);
}

manifold::MeshGL MachineCut::TestCut()
{
	WLOG_FUNCTION_TIMER();

	Manifold terrain = Manifold::Cube(vec3(10.0, 2.0, 10.0), true);

	vector<Manifold> carvingTools;
	double unit_size = 10.0 / 1000;

	for (int j = 0; j < 1; ++j) {

		for (int i = 0; i < 1000; ++i) {
			float x = 5.0f;
			float z = -5.0f + i * unit_size;
			Manifold tool = Manifold::Cube(vec3(0.1f, 0.1f, 0.1f), false).Translate(vec3(-5.0 + j * 0.1, 0.95, z));
			carvingTools.push_back(tool);
		}
		if (j % 4 == 0) {
			Manifold toolCombine;
			manifold_tool_combine(carvingTools, toolCombine);

			terrain -= toolCombine;
			terrain.Simplify(0.1);
			carvingTools.clear();
		}
	}

	{
		Manifold toolCombine;
		manifold_tool_combine(carvingTools, toolCombine);

		terrain -= toolCombine;
		terrain.Simplify(0.01);
		carvingTools.clear();

	}

	MeshGL resultMesh = terrain.GetMeshGL();
	spdlog::info("Resulting mesh has {} triangles.", resultMesh.NumTri());

	// 5. 检查结果有效性
	if (terrain.Status() == Manifold::Error::NoError) {
		//ExportMesh("terrain.glb", terrain.GetMesh(), {});
		spdlog::info("Terrain carving completed successfully.");
	}
	else {
		spdlog::error("Error during boolean operations.");
	}
	return resultMesh;
}
