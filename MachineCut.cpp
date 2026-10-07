
#include "machinecut.h"
#include "meshIO.h"
#include "wlog.h"
#include <set>
#include <tuple>
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

	for (int j = 0; j < 0; ++j) {

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

manifold::MeshGL MachineCut::LoadMesh(const string& filename)
{
	manifold::Manifold mesh = manifold::ImportMesh(filename);
	return mesh.GetMeshGL();
}

void MachineCut::ManifoldTest()
{
	WLOG_FUNCTION_TIMER();

	Manifold terrain = Manifold::Cube(vec3(10.0, 2.0, 10.0), true);
	
	vector<Manifold> carvingTools;
	double unit_size = 10.0 / 1000;

	for (int j = 0; j < 50; ++j) {

		for (int i = 0; i < 1000; ++i) {
			float x = 5.0f;
			float z = -5.0f + i * unit_size;
			Manifold tool = Manifold::Cube(vec3(0.1f, 0.1f, 0.1f), false).Translate(vec3(-5.0 + j * 0.1, 0.95, z));
			carvingTools.push_back(tool);
		}
		Manifold toolCombine;
		manifold_tool_combine(carvingTools, toolCombine);

		// Diff: collect vertex positions before the subtraction.
		const MeshGL meshBefore = terrain.GetMeshGL();
		std::set<std::tuple<float, float, float>> vertsBefore;
		for (uint32_t v = 0; v < meshBefore.NumVert(); ++v)
		{
			const float* p = &meshBefore.vertProperties[v * meshBefore.numProp];
			vertsBefore.emplace(p[0], p[1], p[2]);
		}

		terrain -= toolCombine;
		terrain.Simplify(0.1);

		// Diff: collect vertex positions after the subtraction and classify.
		const MeshGL meshAfter = terrain.GetMeshGL();
		std::set<std::tuple<float, float, float>> vertsAfter;
		for (uint32_t v = 0; v < meshAfter.NumVert(); ++v)
		{
			const float* p = &meshAfter.vertProperties[v * meshAfter.numProp];
			vertsAfter.emplace(p[0], p[1], p[2]);
		}

		std::vector<std::tuple<float, float, float>> removedVerts;
		std::vector<std::tuple<float, float, float>> addedVerts;
		for (const auto& v : vertsBefore)
			if (!vertsAfter.count(v)) removedVerts.push_back(v);
		for (const auto& v : vertsAfter)
			if (!vertsBefore.count(v)) addedVerts.push_back(v);

		spdlog::info("[ManifoldTest] pass {}: before {} verts / {} tris -> after {} verts / {} tris",
			j, meshBefore.NumVert(), meshBefore.NumTri(), meshAfter.NumVert(), meshAfter.NumTri());
		spdlog::info("[ManifoldTest] pass {}: {} vertices removed, {} vertices added (modified = shared keys with changed normals)",
			j, removedVerts.size(), addedVerts.size());

		// Log first few removed/added vertex positions for inspection.
		const size_t maxPrint = 5;
		for (size_t k = 0; k < std::min(removedVerts.size(), maxPrint); ++k)
			spdlog::info("  removed: ({:.3f}, {:.3f}, {:.3f})",
				std::get<0>(removedVerts[k]), std::get<1>(removedVerts[k]), std::get<2>(removedVerts[k]));
		for (size_t k = 0; k < std::min(addedVerts.size(), maxPrint); ++k)
			spdlog::info("  added:   ({:.3f}, {:.3f}, {:.3f})",
				std::get<0>(addedVerts[k]), std::get<1>(addedVerts[k]), std::get<2>(addedVerts[k]));


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
}
