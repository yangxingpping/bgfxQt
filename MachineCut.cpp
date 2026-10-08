
#include "machinecut.h"
#include "meshIO.h"
#include "wlog.h"
#include "buffermanager.h"

#include <set>
#include <fstream>
#include <tuple>
#include <vector>
#include <memory>

using std::tuple;
using std::make_tuple;
using std::make_unique;
using std::unique_ptr;
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


void WriteOBJ(const std::string& filename, const MeshGL& mesh) {
	WLOG_FUNCTION_TIMER();
	std::ofstream file(filename);
	if (!file.is_open()) {
		spdlog::error("cannot open: {}", filename);
		return;
	}

	// 写入顶点
	for (size_t i = 0; i < mesh.NumVert(); ++i) {
		auto pos = mesh.GetVertPos(i);
		file << "v " << pos.x << " " << pos.y << " " << pos.z << "\n";
	}

	// 写入面（OBJ 索引从 1 开始）
	for (size_t i = 0; i < mesh.NumTri(); ++i) {
		auto tri = mesh.GetTriVerts(i);
		file << "f " << (tri[0] + 1) << " " << (tri[1] + 1) << " " << (tri[2] + 1) << "\n";
	}

	file.close();
	spdlog::info("exported: {}", filename);
}

void MachineCut::ManifoldTest()
{
	//WLOG_FUNCTION_TIMER();

	Manifold terrain = Manifold::Cube(vec3(10.0, 2.0, 10.0), true);
	
	double unit_size = 10.0 / 100;
	unique_ptr<BufferFrame> newFrame = nullptr;

	for (int j = 0; j < 100; ++j) {

		for (int i = 0; i < 100; ++i) {
			float x = 5.0f;
			float z = -5.0f + i * unit_size;
			Manifold tool = Manifold::Cube(vec3(0.3f, 0.3f, 0.3f), false).Translate(vec3(-5.0 + j * 0.1, 0.85, z));
			terrain -= tool;
			if (j % 2 == 0) 
			{
				terrain = terrain.AsOriginal();
				terrain.Simplify(0.1);
			}
			manifold::MeshGL mesh = terrain.GetMeshGL();
			
			 uint32_t numVert = uint32_t(mesh.NumVert());
			 uint32_t numTri = uint32_t(mesh.NumTri());
			 //spdlog::info("Uploading manifold mesh to GPU: {} vertices, {} triangles.", numVert, numTri);
			if (numVert == 0 || numTri == 0)
				return;
			if (newFrame == nullptr)
			{
				newFrame = make_unique<BufferFrame>();
			}
			newFrame->vertices.resize(numVert);

			for (uint32_t i = 0; i < numVert; ++i)
			{
				const float* p = &mesh.vertProperties[i * mesh.numProp];
				
				newFrame->vertices[i].x = p[0];
				newFrame->vertices[i].y = p[1];
				newFrame->vertices[i].z = p[2];
				newFrame->vertices[i].abgr = 0xff808080;
			}
			auto& indexs = mesh.triVerts;
			newFrame->indices.assign(indexs.begin(), indexs.end());
			//std::this_thread::sleep_for(std::chrono::milliseconds(1000/60));
			newFrame = BufferManager::putFrame(std::move(newFrame));
		}
		
	}
}
