#include "BgfxWindow.h"

#include <QShowEvent>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <taskflow/taskflow.hpp>

#include "WLog.h"
#include "MachineCut.h"

#include <cmath>
#include <vector>

#include "vs_cube_dx11.bin.h"
#include "fs_cube_dx11.bin.h"
#include "vs_cube_vk.bin.h"
#include "fs_cube_vk.bin.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

tf::Taskflow g_taskflow;
tf::Executor g_executor;

namespace
{

struct PosColorVertex
{
    float    x;
    float    y;
    float    z;
    uint32_t abgr; // RGBA8, little-endian byte order R, G, B, A
};

// Packed model vertex for the transient-buffer draw path:
// position (3 floats) + normal-mapped color (RGBA8).
struct ModelVertex
{
    float    x, y, z;
    uint32_t abgr;
};

static const PosColorVertex s_cubeVertices[] =
{
    {-1.0f,  1.0f,  1.0f, 0xff000000},
    { 1.0f,  1.0f,  1.0f, 0xff0000ff},
    {-1.0f, -1.0f,  1.0f, 0xff00ff00},
    { 1.0f, -1.0f,  1.0f, 0xff00ffff},
    {-1.0f,  1.0f, -1.0f, 0xffff0000},
    { 1.0f,  1.0f, -1.0f, 0xffff00ff},
    {-1.0f, -1.0f, -1.0f, 0xffffff00},
    { 1.0f, -1.0f, -1.0f, 0xffffffff},
};

static const uint16_t s_cubeIndices[] =
{
    0, 1, 2,
    1, 3, 2,
    4, 6, 5,
    5, 6, 7,
    0, 2, 4,
    4, 2, 6,
    1, 5, 3,
    5, 7, 3,
    0, 4, 1,
    4, 5, 1,
    2, 3, 6,
    6, 3, 7,
};

} // namespace

BgfxWindow::BgfxWindow(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);

    resize(1280, 720);
}

BgfxWindow::~BgfxWindow()
{
    shutdownBgfx();
}

QSize BgfxWindow::physicalSize() const
{
    const qreal dpr = devicePixelRatioF();
    return QSize(
        int(width()  * dpr),
        int(height() * dpr)
    );
}

static std::vector<ModelVertex> vertices(2000000);
static std::vector<bx::Vec3> normals(2000000, { 0.0f, 0.0f, 0.0f });

void BgfxWindow::drawModel()
{
    WLOG_FUNCTION_TIMER();
    if (m_manifold.IsEmpty())
        return;

    // Upload the manifold mesh to GPU buffers once (lazy).
    if (!m_modelBuilt)
    {
        SPDLOG_INFO("test 0");
        const manifold::MeshGL& mesh = m_mesh;
        
        const uint32_t numVert = uint32_t(mesh.NumVert());
        const uint32_t numTri  = uint32_t(mesh.NumTri());
		SPDLOG_INFO("Uploading manifold mesh to GPU: {} vertices, {} triangles.", numVert, numTri);

        if (numVert == 0 || numTri == 0)
            return;

        // Compute per-vertex normals by averaging face normals.
        
        for (uint32_t t = 0; t < numTri; ++t)
        {
            const uint32_t i0 = mesh.triVerts[t * 3 + 0];
            const uint32_t i1 = mesh.triVerts[t * 3 + 1];
            const uint32_t i2 = mesh.triVerts[t * 3 + 2];

            const float* p0 = &mesh.vertProperties[i0 * mesh.numProp];
            const float* p1 = &mesh.vertProperties[i1 * mesh.numProp];
            const float* p2 = &mesh.vertProperties[i2 * mesh.numProp];

            const bx::Vec3 e1 = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
            const bx::Vec3 e2 = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            bx::Vec3 n = bx::cross(e1, e2);
            const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
            if (len2 > 1e-12f)
            {
                const float invLen = 1.0f / bx::sqrt(len2);
                n = {n.x * invLen, n.y * invLen, n.z * invLen};
            }
            else
            {
                n = {0.0f, 1.0f, 0.0f};
            }

            normals[i0] = bx::add(normals[i0], n);
            normals[i1] = bx::add(normals[i1], n);
            normals[i2] = bx::add(normals[i2], n);
        }
        SPDLOG_INFO("test 1");
        for (uint32_t i = 0; i < numVert; ++i)
        {
            bx::Vec3& n = normals[i];
            const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
            if (len2 > 1e-12f)
            {
                const float invLen = 1.0f / bx::sqrt(len2);
                n = {n.x * invLen, n.y * invLen, n.z * invLen};
            }
            else
            {
                n = {0.0f, 1.0f, 0.0f};
            }
        }
        SPDLOG_INFO("test 2");
        // Pack position (3 floats) + color (RGBA8); ModelVertex is defined in
        // the file-scope anonymous namespace.
        
        for (uint32_t i = 0; i < numVert; ++i)
        {
            const float* p = &mesh.vertProperties[i * mesh.numProp];
            const bx::Vec3& n = normals[i];

            // Map normal direction to a color (n * 0.5 + 0.5) for a shaded look.
            const uint8_t r = uint8_t(bx::clamp(n.x * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
            const uint8_t g = uint8_t(bx::clamp(n.y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
            const uint8_t b = uint8_t(bx::clamp(n.z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
            const uint8_t a = 255;

            vertices[i].x = p[0];
            vertices[i].y = p[1];
            vertices[i].z = p[2];
            vertices[i].abgr =
                (uint32_t(a) << 24) |
                (uint32_t(b) << 16) |
                (uint32_t(g) << 8)  |
                 uint32_t(r);
        }
        SPDLOG_INFO("test 3");
        m_modelLayout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
            .end();
        SPDLOG_INFO("test 4");
        m_modelVbh = bgfx::createVertexBuffer(
            bgfx::copy(vertices.data(), uint32_t(numVert * sizeof(ModelVertex))),
            m_modelLayout
        );
        SPDLOG_INFO("test 5");
        m_modelIbh = bgfx::createIndexBuffer(
            bgfx::copy(mesh.triVerts.data(), uint32_t(mesh.triVerts.size() * sizeof(uint32_t))),
            BGFX_BUFFER_INDEX32
        );
        SPDLOG_INFO("test 6");
        m_modelIndexCount = numTri * 3;
        m_modelBuilt = true;
    }

    float model[16];
    bx::mtxIdentity(model);

    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, m_modelVbh);
    bgfx::setIndexBuffer(m_modelIbh);
    // Depth test + RGB write, no face culling so every face is drawn.
    bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
    bgfx::submit(0, m_program);
}

void BgfxWindow::drawModelWithTransient()
{
	WLOG_FUNCTION_TIMER();
	if (m_manifold.IsEmpty())
		return;

	// Pack the manifold mesh into a CPU-side cache once (lazy). The cache is
	// then copied into bgfx's transient buffers on every frame below.
	if (!m_transientBuilt)
	{
		SPDLOG_INFO("test 0");
		const manifold::MeshGL& mesh = m_mesh;

		const uint32_t numVert = uint32_t(mesh.NumVert());
		const uint32_t numTri = uint32_t(mesh.NumTri());
		SPDLOG_INFO("Uploading manifold mesh to GPU: {} vertices, {} triangles.", numVert, numTri);

		if (numVert == 0 || numTri == 0)
			return;

		// Compute per-vertex normals by averaging face normals.
		std::vector<bx::Vec3> normals(numVert, { 0.0f, 0.0f, 0.0f });
		for (uint32_t t = 0; t < numTri; ++t)
		{
			break;
			const uint32_t i0 = mesh.triVerts[t * 3 + 0];
			const uint32_t i1 = mesh.triVerts[t * 3 + 1];
			const uint32_t i2 = mesh.triVerts[t * 3 + 2];

			const float* p0 = &mesh.vertProperties[i0 * mesh.numProp];
			const float* p1 = &mesh.vertProperties[i1 * mesh.numProp];
			const float* p2 = &mesh.vertProperties[i2 * mesh.numProp];

			const bx::Vec3 e1 = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
			const bx::Vec3 e2 = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
			bx::Vec3 n = bx::cross(e1, e2);
			const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
			if (len2 > 1e-12f)
			{
				const float invLen = 1.0f / bx::sqrt(len2);
				n = { n.x * invLen, n.y * invLen, n.z * invLen };
			}
			else
			{
				n = { 0.0f, 1.0f, 0.0f };
			}

			normals[i0] = bx::add(normals[i0], n);
			normals[i1] = bx::add(normals[i1], n);
			normals[i2] = bx::add(normals[i2], n);
		}
		SPDLOG_INFO("test 1");
		for (uint32_t i = 0; i < numVert; ++i)
		{
			bx::Vec3& n = normals[i];
			const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
			if (len2 > 1e-12f)
			{
				const float invLen = 1.0f / bx::sqrt(len2);
				n = { n.x * invLen, n.y * invLen, n.z * invLen };
			}
			else
			{
				n = { 0.0f, 1.0f, 0.0f };
			}
		}
		SPDLOG_INFO("test 2");
		// Pack position (3 floats) + color (RGBA8); ModelVertex is defined in
		// the file-scope anonymous namespace.

		std::vector<ModelVertex> vertices(numVert);
		for (uint32_t i = 0; i < numVert; ++i)
		{
			const float* p = &mesh.vertProperties[i * mesh.numProp];
			const bx::Vec3& n = normals[i];

			// Map normal direction to a color (n * 0.5 + 0.5) for a shaded look.
			const uint8_t r = uint8_t(bx::clamp(n.x * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
			const uint8_t g = uint8_t(bx::clamp(n.y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
			const uint8_t b = uint8_t(bx::clamp(n.z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
			const uint8_t a = 255;

			vertices[i].x = p[0];
			vertices[i].y = p[1];
			vertices[i].z = p[2];
			vertices[i].abgr =
				(uint32_t(a) << 24) |
				(uint32_t(b) << 16) |
				(uint32_t(g) << 8) |
				uint32_t(r);
		}
		SPDLOG_INFO("test 3");
		m_modelLayout
			.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();
		SPDLOG_INFO("test 4");
		// Transient buffers are per-frame scratch memory, so the packed mesh
		// is cached on the CPU here (once) and copied into the transient pool
		// every frame below. No persistent GPU buffers are created.
		m_transientVertexData.assign(
			reinterpret_cast<const uint8_t*>(vertices.data()),
			reinterpret_cast<const uint8_t*>(vertices.data()) + vertices.size() * sizeof(ModelVertex));
		m_transientIndexData.assign(mesh.triVerts.begin(), mesh.triVerts.end());
		m_transientVertexCount = numVert;
		SPDLOG_INFO("test 6");
		m_transientBuilt = true;
	}

	if (m_transientVertexCount == 0 || m_transientIndexData.empty())
		return;

	const uint32_t vertCount = m_transientVertexCount;
	const uint32_t idxCount  = uint32_t(m_transientIndexData.size());

	// Every frame: reserve scratch space from bgfx's transient pools and copy
	// the cached mesh into it. The allocations are only valid until the next
	// bgfx::frame() call. Returns false when the pool lacks space this frame.
	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer  tib;
	if (!bgfx::allocTransientBuffers(
			&tvb, m_modelLayout, vertCount,
			&tib, idxCount, /*_index32*/ true))
	{
		SPDLOG_WARN("Transient buffer pool exhausted ({} verts, {} indices); skipping model draw this frame.",
					vertCount, idxCount);
		return;
	}

	bx::memCopy(tvb.data, m_transientVertexData.data(), vertCount * sizeof(ModelVertex));
	bx::memCopy(tib.data, m_transientIndexData.data(), idxCount * sizeof(uint32_t));

	float model[16];
	bx::mtxIdentity(model);

	bgfx::setTransform(model);
	bgfx::setVertexBuffer(0, &tvb);
	bgfx::setIndexBuffer(&tib);
	// Depth test + RGB write, no face culling so every face is drawn.
	bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
	bgfx::submit(0, m_program);
}

void BgfxWindow::drawAxis3D()
{
    // Build the axis gizmo once: three colored axes (X red, Y green, Z blue),
    // each a box shaft with a square-pyramid tip, generated around the local
    // origin. The whole gizmo is then translated to the orbit target.
    if (!m_axisBuilt)
    {
        struct AxisVertex
        {
            float    x, y, z;
            uint32_t abgr;
        };

        std::vector<AxisVertex> verts;
        std::vector<uint16_t>   indices;

        auto packColor = [](uint8_t r, uint8_t g, uint8_t b) -> uint32_t
        {
            return (uint32_t(255) << 24) |
                   (uint32_t(b)   << 16) |
                   (uint32_t(g)   << 8)  |
                    uint32_t(r);
        };

        const uint32_t colX = packColor(220,  60,  60);
        const uint32_t colY = packColor( 60, 200,  60);
        const uint32_t colZ = packColor( 60, 110, 230);

        // Dimensions.
        const float shaftLen   = 1.6f;
        const float shaftHalf  = 0.0175f;
        const float tipLen     = 0.30f;
        const float tipRadius  = 0.055f;

        auto addBox = [&](uint32_t color,
                          float x0, float y0, float z0,
                          float x1, float y1, float z1)
        {
            const uint32_t base = uint32_t(verts.size());
            verts.push_back({x0, y0, z0, color});
            verts.push_back({x1, y0, z0, color});
            verts.push_back({x1, y1, z0, color});
            verts.push_back({x0, y1, z0, color});
            verts.push_back({x0, y0, z1, color});
            verts.push_back({x1, y0, z1, color});
            verts.push_back({x1, y1, z1, color});
            verts.push_back({x0, y1, z1, color});

            const uint16_t b[8] = {
                uint16_t(base+0), uint16_t(base+1), uint16_t(base+2), uint16_t(base+3),
                uint16_t(base+4), uint16_t(base+5), uint16_t(base+6), uint16_t(base+7)
            };
            // -Z, +Z, -Y, +Y, -X, +X
            indices.insert(indices.end(), {b[0],b[3],b[2], b[0],b[2],b[1]});
            indices.insert(indices.end(), {b[4],b[5],b[6], b[4],b[6],b[7]});
            indices.insert(indices.end(), {b[0],b[1],b[5], b[0],b[5],b[4]});
            indices.insert(indices.end(), {b[3],b[7],b[6], b[3],b[6],b[2]});
            indices.insert(indices.end(), {b[0],b[4],b[7], b[0],b[7],b[3]});
            indices.insert(indices.end(), {b[1],b[2],b[6], b[1],b[6],b[5]});
        };

        // addPyramid: square base at (cx,cy,cz) in the plane perpendicular to
        // axis 'dir', apex extending tipLen beyond the base along 'dir'.
        // dir is one of +X, +Y, +Z.
        auto addPyramid = [&](uint32_t color, char dir,
                              float cx, float cy, float cz,
                              float radius, float len)
        {
            float b0[3], b1[3], b2[3], b3[3], apex[3];
            if (dir == 'x')
            {
                b0[0]=cx; b0[1]=cy+radius; b0[2]=cz+radius;
                b1[0]=cx; b1[1]=cy-radius; b1[2]=cz+radius;
                b2[0]=cx; b2[1]=cy-radius; b2[2]=cz-radius;
                b3[0]=cx; b3[1]=cy+radius; b3[2]=cz-radius;
                apex[0]=cx+len; apex[1]=cy; apex[2]=cz;
            }
            else if (dir == 'y')
            {
                b0[0]=cx+radius; b0[1]=cy; b0[2]=cz+radius;
                b1[0]=cx-radius; b1[1]=cy; b1[2]=cz+radius;
                b2[0]=cx-radius; b2[1]=cy; b2[2]=cz-radius;
                b3[0]=cx+radius; b3[1]=cy; b3[2]=cz-radius;
                apex[0]=cx; apex[1]=cy+len; apex[2]=cz;
            }
            else // 'z'
            {
                b0[0]=cx+radius; b0[1]=cy+radius; b0[2]=cz;
                b1[0]=cx-radius; b1[1]=cy+radius; b1[2]=cz;
                b2[0]=cx-radius; b2[1]=cy-radius; b2[2]=cz;
                b3[0]=cx+radius; b3[1]=cy-radius; b3[2]=cz;
                apex[0]=cx; apex[1]=cy; apex[2]=cz+len;
            }

            const uint32_t base = uint32_t(verts.size());
            verts.push_back({b0[0], b0[1], b0[2], color});
            verts.push_back({b1[0], b1[1], b1[2], color});
            verts.push_back({b2[0], b2[1], b2[2], color});
            verts.push_back({b3[0], b3[1], b3[2], color});
            verts.push_back({apex[0], apex[1], apex[2], color});

            const uint16_t i0 = uint16_t(base+0);
            const uint16_t i1 = uint16_t(base+1);
            const uint16_t i2 = uint16_t(base+2);
            const uint16_t i3 = uint16_t(base+3);
            const uint16_t ia = uint16_t(base+4);
            // Base cap + four side faces.
            indices.insert(indices.end(), {i0, i2, i1, i0, i3, i2});
            indices.insert(indices.end(), {i0, i1, ia, i1, i2, ia,
                                            i2, i3, ia, i3, i0, ia});
        };

        // X axis (red)
        addBox    (colX, 0.0f, -shaftHalf, -shaftHalf, shaftLen, shaftHalf, shaftHalf);
        addPyramid(colX, 'x', shaftLen, 0.0f, 0.0f, tipRadius, tipLen);

        // Y axis (green)
        addBox    (colY, -shaftHalf, 0.0f, -shaftHalf, shaftHalf, shaftLen, shaftHalf);
        addPyramid(colY, 'y', 0.0f, shaftLen, 0.0f, tipRadius, tipLen);

        // Z axis (blue)
        addBox    (colZ, -shaftHalf, -shaftHalf, 0.0f, shaftHalf, shaftHalf, shaftLen);
        addPyramid(colZ, 'z', 0.0f, 0.0f, shaftLen, tipRadius, tipLen);

        m_axisLayout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
            .end();

        m_axisVbh = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(AxisVertex))),
            m_axisLayout
        );

        m_axisIbh = bgfx::createIndexBuffer(
            bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t)))
        );

        m_axisIndexCount = uint32_t(indices.size());
        m_axisBuilt = true;
    }

    // Draw the axes at the world origin, same as the model. Because the orbit
    // camera looks at m_target (which moves during panning), both the model and
    // the axes slide together on screen when the user right-drags.
    float model[16];
    bx::mtxIdentity(model);

    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, m_axisVbh);
    bgfx::setIndexBuffer(m_axisIbh);
    bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
    bgfx::submit(0, m_program);
}


void BgfxWindow::drawLight()
{
    // Build the light gizmo once: a small UV sphere rendered with an emissive
    // daylight color (~6500K warm white) so it reads as a glowing light bulb.
    if (!m_lightBuilt)
    {
        struct LightVertex
        {
            float    x, y, z;
            uint32_t abgr;
        };

        std::vector<LightVertex> verts;
        std::vector<uint16_t>    indices;

        // Daylight color (R=255, G=250, B=235, A=255).
        const uint32_t col =
            (uint32_t(255) << 24) |
            (uint32_t(235) << 16) |
            (uint32_t(250) << 8)  |
             uint32_t(255);

        const float radius  = 0.5f;
        const int   stacks  = 12;
        const int   slices  = 16;

        // Vertices: (stacks+1) rings of (slices+1) vertices each.
        for (int i = 0; i <= stacks; ++i)
        {
            const float phi = bx::kPi * float(i) / float(stacks); // 0..pi
            const float y   = radius * bx::cos(phi);
            const float r   = radius * bx::sin(phi);
            for (int j = 0; j <= slices; ++j)
            {
                const float theta = 2.0f * bx::kPi * float(j) / float(slices);
                verts.push_back({ r * bx::cos(theta), y, r * bx::sin(theta), col });
            }
        }

        // Indices: two triangles per quad.
        for (int i = 0; i < stacks; ++i)
        {
            for (int j = 0; j < slices; ++j)
            {
                const uint16_t a = uint16_t(i * (slices + 1) + j);
                const uint16_t b = uint16_t(a + slices + 1);
                indices.insert(indices.end(), {
                    a, b, uint16_t(a + 1),
                    uint16_t(a + 1), b, uint16_t(b + 1)
                });
            }
        }

        m_lightLayout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
            .end();

        m_lightVbh = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(LightVertex))),
            m_lightLayout
        );

        m_lightIbh = bgfx::createIndexBuffer(
            bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t)))
        );

        m_lightIndexCount = uint32_t(indices.size());
        m_lightBuilt = true;
    }

    // Place the light sphere at the daylight point-light position (50,50,50).
    float model[16];
    bx::mtxTranslate(model, m_lightPos.x, m_lightPos.y, m_lightPos.z);

    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, m_lightVbh);
    bgfx::setIndexBuffer(m_lightIbh, 0, m_lightIndexCount);
    bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
    bgfx::submit(0, m_program);
}


void BgfxWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (!m_bgfxInitialized)
        initBgfx();
}

bool BgfxWindow::initBgfx()
{
    if (m_bgfxInitialized)
        return true;

#ifdef Q_OS_WIN

    HWND hwnd = reinterpret_cast<HWND>(winId());

    bgfx::Init init;

    init.type = bgfx::RendererType::Count;

    init.platformData.nwh = hwnd;


    const QSize fbSize = physicalSize();

    init.resolution.width  = fbSize.width();
    init.resolution.height = fbSize.height();

    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init))
    {
        return false;
    }

#endif

    bgfx::setDebug(BGFX_DEBUG_STATS);

    m_bgfxInitialized = true;

    if (!initCube())
        return false;

    // Navigation cube shares the same position+color shader as the cube.
    m_navCube.init(m_program);

    // Sample manifold model rendered by drawModel().
	//m_manifold = manifold::Manifold::Sphere(1.0f);
    m_manifold = manifold::Manifold::Cube(manifold::vec3(1.0f, 1.0f, 1.0f));



    m_manifold = manifold::Manifold(MachineCut::TestCut());
	//m_manifold = manifold::Manifold(MachineCut::LoadMesh("stl.stl"));
    m_mesh = MachineCut::LoadMesh("dd.stl");

    m_renderTimer = new QTimer(this);
    connect(m_renderTimer, &QTimer::timeout, this, &BgfxWindow::renderFrame);
    m_renderTimer->start(16);

    return true;
}

bool BgfxWindow::initCube()
{
    if (m_cubeInitialized)
        return true;

    // Pick the shader blob matching the renderer bgfx selected at runtime.
    const uint8_t* vsData = nullptr;
    uint32_t       vsSize = 0;
    const uint8_t* fsData = nullptr;
    uint32_t       fsSize = 0;

    switch (bgfx::getRendererType())
    {
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12:
            vsData = vs_cube_dx11;
            vsSize = sizeof(vs_cube_dx11);
            fsData = fs_cube_dx11;
            fsSize = sizeof(fs_cube_dx11);
            break;

        case bgfx::RendererType::Vulkan:
            vsData = vs_cube_vk;
            vsSize = sizeof(vs_cube_vk);
            fsData = fs_cube_vk;
            fsSize = sizeof(fs_cube_vk);
            break;

        default:
            return false;
    }

    m_vertexShader = bgfx::createShader(bgfx::copy(vsData, vsSize));
    m_fragmentShader = bgfx::createShader(bgfx::copy(fsData, fsSize));
    m_program = bgfx::createProgram(m_vertexShader, m_fragmentShader, true);

    m_layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
        .end();

    m_vbh = bgfx::createVertexBuffer(
        bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices)),
        m_layout
    );

    m_ibh = bgfx::createIndexBuffer(
        bgfx::makeRef(s_cubeIndices, sizeof(s_cubeIndices))
    );

    m_cubeInitialized = true;
    g_executor.async([this]() {
        MachineCut::ManifoldTest();
        });
    return true;
}

void BgfxWindow::destroyCube()
{
    if (!m_cubeInitialized)
        return;

    if (bgfx::isValid(m_vbh))
        bgfx::destroy(m_vbh);
    if (bgfx::isValid(m_ibh))
        bgfx::destroy(m_ibh);
    if (bgfx::isValid(m_program))
        bgfx::destroy(m_program);

    m_vbh     = BGFX_INVALID_HANDLE;
    m_ibh     = BGFX_INVALID_HANDLE;
    m_program = BGFX_INVALID_HANDLE;

    m_cubeInitialized = false;
}

void BgfxWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (!m_bgfxInitialized)
        return;

    const QSize fbSize = physicalSize();

    bgfx::reset(
        fbSize.width(),
        fbSize.height(),
        BGFX_RESET_VSYNC
    );
}

void BgfxWindow::renderFrame()
{
    if (!m_bgfxInitialized)
        return;

    bgfx::setViewClear(
        0,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        0x303030ff,
        1.0f,
        0
    );

    const QSize fbSize = physicalSize();

    bgfx::setViewRect(
        0,
        0,
        0,
        uint16_t(fbSize.width()),
        uint16_t(fbSize.height())
    );

    // Orbit camera: rotate around m_target (which is moved by right-drag panning).
    // bgfx is right-handed with the camera looking towards +Z, so at yaw=0
    // the camera sits on the negative Z axis relative to the target.
    const bx::Vec3 at = m_target;
    const bx::Vec3 eye =
    {
        m_target.x + m_cameraDistance * bx::sin(m_cameraYaw)   * bx::cos(m_cameraPitch),
        m_target.y + m_cameraDistance * bx::sin(m_cameraPitch),
        m_target.z - m_cameraDistance * bx::cos(m_cameraYaw)   * bx::cos(m_cameraPitch)
    };

    float view[16];
    bx::mtxLookAt(view, eye, at);

    // Orthographic projection.  Half-height is derived from the orbit distance
    // using the old 60deg perspective FOV (tan 30deg) so the initial framing
    // is unchanged and the mouse wheel still acts as zoom -- moving the camera
    // farther now widens the ortho frustum instead of making objects smaller.
    float proj[16];
    const float aspect    = float(fbSize.width()) / float(fbSize.height());
    const float halfH     = m_cameraDistance * bx::tan(60.0f * bx::kPi / 360.0f);
    const float halfW     = halfH * aspect;
    bx::mtxOrtho(
        proj,
        -halfW, halfW,
        -halfH, halfH,
        0.1f,
        1000.0f,
        0.0f,
        bgfx::getCaps()->homogeneousDepth
    );

    bgfx::setViewTransform(0, view, proj);

    bgfx::touch(0);

    drawModel();
    //drawModelWithTransient();
    drawAxis3D();
    drawLight();

    // Render the navigation cube overlay in the top-left corner (view 1).
    //m_navCube.render(1, m_cameraYaw, m_cameraPitch, uint16_t(fbSize.width()), uint16_t(fbSize.height()));

    bgfx::frame();
}

void BgfxWindow::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);

    const QPoint pos = event->position().toPoint();

    // Convert widget-local (DIP) coordinates to physical pixels so they match
    // the bgfx framebuffer coordinates used by the navigation cube.
    const qreal dpr = devicePixelRatioF();
    const int   px  = int(pos.x() * dpr);
    const int   py  = int(pos.y() * dpr);

    if (event->button() == Qt::LeftButton)
    {
        int face = -1;
        const QSize fb = physicalSize();
        if (m_navCube.hitTest(px, py, uint16_t(fb.width()), uint16_t(fb.height()), face))
        {
            // Snap the camera to look along the clicked face's direction.
            // Face 0-5: main faces (+X, -X, +Y, -Y, +Z, -Z)
            // Face 6-13: corner chamfers (8 corners at (±1, ±1, ±1))
            // Face 14-25: edge chamfers (12 edges)
            if (face >= 0 && face <= 5)
            {
                // Main faces.  eye offset = (sin(yaw)cos(pitch), sin(pitch),
                // -cos(yaw)cos(pitch)) * d; to look AT a face the camera must
                // be placed on that face's side, e.g. clicking +X puts the
                // camera at +X (yaw = +pi/2), not the opposite side.
                switch (face)
                {
                    case 0: m_cameraYaw =  bx::kPi * 0.5f; m_cameraPitch = 0.0f; break; // +X (right)
                    case 1: m_cameraYaw = -bx::kPi * 0.5f; m_cameraPitch = 0.0f; break; // -X (left)
                    case 2: m_cameraYaw = 0.0f;            m_cameraPitch =  bx::kPi * 0.5f; break; // +Y (rear)
                    case 3: m_cameraYaw = 0.0f;            m_cameraPitch = -bx::kPi * 0.5f; break; // -Y (front)
                    case 4: m_cameraYaw = bx::kPi;         m_cameraPitch = 0.0f; break; // +Z (top)
                    case 5: m_cameraYaw = 0.0f;            m_cameraPitch = 0.0f; break; // -Z (bottom)
                }
            }
            else if (face >= 6 && face <= 13)
            {
                // Corner chamfers: decode corner index to direction.
                // cornerIdx: bit 0 = sx (+1 if set), bit 1 = sy, bit 2 = sz.
                const int cornerIdx = face - 6;
                const float sx = (cornerIdx & 1) ? 1.0f : -1.0f;
                const float sy = (cornerIdx & 2) ? 1.0f : -1.0f;
                const float sz = (cornerIdx & 4) ? 1.0f : -1.0f;
                // Camera looks from corner toward center: direction is (-sx, -sy, -sz).
                // yaw = atan2(-dx, dz) where (dx,dy,dz) = (-sx,-sy,-sz), so yaw = atan2(sx, -sz).
                m_cameraYaw = std::atan2(sx, -sz);
                m_cameraPitch = std::atan2(sy, std::sqrt(sx * sx + sz * sz));
            }
            else if (face >= 14 && face <= 25)
            {
                // Edge chamfers: decode edge index to direction.
                const int edgeIdx = face - 14;
                float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                if (edgeIdx < 4)
                {
                    // Edges parallel to Z at (±1, ±1, *).
                    sx = (edgeIdx & 1) ? 1.0f : -1.0f;
                    sy = (edgeIdx & 2) ? 1.0f : -1.0f;
                }
                else if (edgeIdx < 8)
                {
                    // Edges parallel to Y at (±1, *, ±1).
                    const int e = edgeIdx - 4;
                    sx = (e & 1) ? 1.0f : -1.0f;
                    sz = (e & 2) ? 1.0f : -1.0f;
                }
                else
                {
                    // Edges parallel to X at (*, ±1, ±1).
                    const int e = edgeIdx - 8;
                    sy = (e & 1) ? 1.0f : -1.0f;
                    sz = (e & 2) ? 1.0f : -1.0f;
                }
                // Camera looks from edge toward center: direction is (-sx, -sy, -sz).
                // yaw = atan2(sx, -sz), pitch = atan2(sy, sqrt(sx² + sz²)).
                m_cameraYaw = std::atan2(sx, -sz);
                m_cameraPitch = std::atan2(sy, std::sqrt(sx * sx + sz * sz));
            }
            return;
        }

        m_leftDragging  = true;
        m_lastMousePos  = pos;
        setCursor(Qt::ClosedHandCursor);
    }
    else if (event->button() == Qt::RightButton)
    {
        m_rightDragging = true;
        m_lastMousePos  = pos;
        setCursor(Qt::SizeAllCursor);
    }
}

void BgfxWindow::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);

    if (!m_leftDragging && !m_rightDragging)
        return;

    const QPoint pos   = event->position().toPoint();
    const QPoint delta = pos - m_lastMousePos;
    m_lastMousePos = pos;

    if (m_leftDragging)
    {
        // Horizontal drag -> yaw. Inverting the sign so the model rotates in
        // the same direction as the cursor (drag right -> model turns right).
        m_cameraYaw   -= float(delta.x()) * 0.005f;
        m_cameraPitch += float(delta.y()) * 0.005f;

        // Clamp pitch so the camera cannot flip over the poles.
        const float pitchLimit = bx::kPi * 0.49f;
        m_cameraPitch = bx::clamp(m_cameraPitch, -pitchLimit, pitchLimit);
    }

    if (m_rightDragging)
    {
        // Camera basis vectors derived from the current yaw/pitch.
        const float cy = bx::cos(m_cameraYaw);
        const float sy = bx::sin(m_cameraYaw);
        const float cp = bx::cos(m_cameraPitch);
        const float sp = bx::sin(m_cameraPitch);

        const bx::Vec3 forward = {-sy * cp, -sp, cy * cp};
        const bx::Vec3 worldUp = {0.0f, 1.0f, 0.0f};
        bx::Vec3 right = bx::cross(forward, worldUp);
        right = bx::normalize(right);
        bx::Vec3 up = bx::cross(right, forward);

        // Pan scale grows with distance so panning feels consistent at any zoom.
        const float scale = m_cameraDistance * 0.0015f;

        // Qt screen Y points down, so a negative delta.y (dragging up) must
        // move the target down (camera down) so the model follows the cursor.
        const float panX = float(delta.x());
        const float panY = float(delta.y());

        m_target.x += right.x * panX * scale + up.x * panY * scale;
        m_target.y += right.y * panX * scale + up.y * panY * scale;
        m_target.z += right.z * panX * scale + up.z * panY * scale;
    }
}

void BgfxWindow::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton)
    {
        m_leftDragging = false;
    }
    else if (event->button() == Qt::RightButton)
    {
        m_rightDragging = false;
    }

    if (!m_leftDragging && !m_rightDragging)
        setCursor(Qt::ArrowCursor);
}

void BgfxWindow::wheelEvent(QWheelEvent* event)
{
    QWidget::wheelEvent(event);

    // angleDelta is in eighths of a degree; a typical notch is 120.
    const float step = float(event->angleDelta().y()) / 120.0f * 0.5f;
    m_cameraDistance = bx::clamp(m_cameraDistance - step, 1.5f, 50.0f);
}

void BgfxWindow::closeEvent(QCloseEvent* event)
{
    shutdownBgfx();

    QWidget::closeEvent(event);
}

void BgfxWindow::shutdownBgfx()
{
    if (!m_bgfxInitialized)
        return;

    if (m_renderTimer != nullptr)
    {
        m_renderTimer->stop();
    }

    destroyCube();
    m_navCube.destroy();

    if (bgfx::isValid(m_modelVbh)) bgfx::destroy(m_modelVbh);
    if (bgfx::isValid(m_modelIbh)) bgfx::destroy(m_modelIbh);
    m_modelVbh = BGFX_INVALID_HANDLE;
    m_modelIbh = BGFX_INVALID_HANDLE;
    m_modelBuilt = false;

    if (bgfx::isValid(m_axisVbh)) bgfx::destroy(m_axisVbh);
    if (bgfx::isValid(m_axisIbh)) bgfx::destroy(m_axisIbh);
    m_axisVbh = BGFX_INVALID_HANDLE;
    m_axisIbh = BGFX_INVALID_HANDLE;
    m_axisBuilt = false;

    if (bgfx::isValid(m_lightVbh)) bgfx::destroy(m_lightVbh);
    if (bgfx::isValid(m_lightIbh)) bgfx::destroy(m_lightIbh);
    m_lightVbh = BGFX_INVALID_HANDLE;
    m_lightIbh = BGFX_INVALID_HANDLE;
    m_lightBuilt = false;

    bgfx::shutdown();

    m_bgfxInitialized = false;
}
