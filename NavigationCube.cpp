#include "NavigationCube.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <manifold/manifold.h>

#include <QImage>
#include <QPainter>
#include <QFont>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

#include "vs_textured_dx11.bin.h"
#include "fs_textured_dx11.bin.h"
#include "vs_textured_vk.bin.h"
#include "fs_textured_vk.bin.h"

namespace
{

// Chamfer width = 1/5 of the cube edge length. The cube spans [-1, 1] so its
// edge length is 2; each corner is cut by the plane sx*x + sy*y + sz*z =
// 3 - kChamfer, giving flat 45-degree bevels on all edges and corners.
constexpr float kChamfer = 2.0f / 5.0f;

// Orthographic half-extent (cube is [-1,1], so [-2,2] leaves a margin).
constexpr float kOrthoHalf = 2.0f;
// Distance of the nav camera from the cube.
constexpr float kCamDist = 5.0f;

// Single base color for the whole cube (light gray). Text is drawn on top in
// a darker shade; the text-quad background matches this color exactly so the
// labels blend seamlessly into the faces.
constexpr uint8_t kCubeR = 224;
constexpr uint8_t kCubeG = 224;
constexpr uint8_t kCubeB = 228;

// Face definitions for the text labels.
//   face 0: +X -> "right"
//   face 1: -X -> "left"
//   face 2: +Y -> "top"
//   face 3: -Y -> "bottom"
//   face 4: +Z -> "near"
//   face 5: -Z -> "far"
// For each face: center, outward normal, quad right-axis, quad up-axis.
struct FaceDef
{
    const char* label;
    bx::Vec3    center;
    bx::Vec3    normal;
    bx::Vec3    right;
    bx::Vec3    up;
    int         atlasCol; // 0..2
    int         atlasRow; // 0..1
};

const FaceDef kFaces[6] =
{
    { "right",  { 1.0f, 0.0f, 0.0f}, { 1, 0, 0}, { 0, 0,-1}, { 0, 1, 0}, 1, 0 },
    { "left",   {-1.0f, 0.0f, 0.0f}, {-1, 0, 0}, { 0, 0, 1}, { 0, 1, 0}, 0, 0 },
    { "top",    { 0.0f, 1.0f, 0.0f}, { 0, 1, 0}, { 1, 0, 0}, { 0, 0,-1}, 2, 0 },
    { "bottom", { 0.0f,-1.0f, 0.0f}, { 0,-1, 0}, { 1, 0, 0}, { 0, 0, 1}, 0, 1 },
    { "near",   { 0.0f, 0.0f, 1.0f}, { 0, 0, 1}, { 1, 0, 0}, { 0, 1, 0}, 1, 1 },
    { "far",    { 0.0f, 0.0f,-1.0f}, { 0, 0,-1}, {-1, 0, 0}, { 0, 1, 0}, 2, 1 },
};

} // namespace

NavigationCube::NavigationCube() = default;

NavigationCube::~NavigationCube()
{
    destroy();
}

void NavigationCube::init(bgfx::ProgramHandle program)
{
    if (m_initialized)
        return;

    m_program = program;

    // ---- Solid chamfered cube (single color) -----------------------------
    manifold::Manifold mesh = manifold::Manifold::Cube({2.0, 2.0, 2.0}, true);
    const double kInvSqrt3 = 1.0 / std::sqrt(3.0);
    const double planeOffset = -(3.0 - double(kChamfer)) * kInvSqrt3;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2)
            {
                mesh = mesh.TrimByPlane(
                    manifold::vec3(
                        double(-sx) * kInvSqrt3,
                        double(-sy) * kInvSqrt3,
                        double(-sz) * kInvSqrt3
                    ),
                    planeOffset
                );
            }

    const manifold::MeshGL gl = mesh.GetMeshGL();
    const uint32_t numVert = uint32_t(gl.NumVert());
    const uint32_t numTri  = uint32_t(gl.NumTri());

    if (numVert == 0 || numTri == 0)
        return;

    const uint32_t cubeAbgr =
        (uint32_t(255)   << 24) |
        (uint32_t(kCubeB) << 16) |
        (uint32_t(kCubeG) << 8)  |
         uint32_t(kCubeR);

    struct Vertex
    {
        float    x, y, z;
        uint32_t abgr;
    };

    std::vector<Vertex> vertices(numVert);
    for (uint32_t i = 0; i < numVert; ++i)
    {
        const float* p = &gl.vertProperties[i * gl.numProp];
        vertices[i].x    = p[0];
        vertices[i].y    = p[1];
        vertices[i].z    = p[2];
        vertices[i].abgr = cubeAbgr; // single color for the whole cube
    }

    m_layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
        .end();

    m_vbh = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(Vertex))),
        m_layout
    );

    m_ibh = bgfx::createIndexBuffer(
        bgfx::copy(gl.triVerts.data(), uint32_t(gl.triVerts.size() * sizeof(uint32_t))),
        BGFX_BUFFER_INDEX32
    );

    m_indexCount = numTri * 3;

    // ---- Black wireframe edges (line list) ---------------------------------
    // Extract every unique edge from the triangle list. Adjacent triangles
    // share an edge, so we deduplicate by storing the sorted index pair.
    {
        std::set<std::pair<uint32_t, uint32_t>> edgeSet;
        for (uint32_t t = 0; t < numTri; ++t)
        {
            const uint32_t i0 = gl.triVerts[t * 3 + 0];
            const uint32_t i1 = gl.triVerts[t * 3 + 1];
            const uint32_t i2 = gl.triVerts[t * 3 + 2];
            auto addEdge = [&](uint32_t a, uint32_t b)
            {
                if (a > b) std::swap(a, b);
                edgeSet.insert({a, b});
            };
            addEdge(i0, i1);
            addEdge(i1, i2);
            addEdge(i2, i0);
        }

        const uint32_t blackAbgr = (uint32_t(255) << 24); // A=255, B=G=R=0

        std::vector<Vertex>   edgeVerts;
        std::vector<uint16_t> edgeIdx;
        edgeVerts.reserve(edgeSet.size() * 2);
        edgeIdx.reserve(edgeSet.size() * 2);

        for (const auto& e : edgeSet)
        {
            const float* pa = &gl.vertProperties[e.first  * gl.numProp];
            const float* pb = &gl.vertProperties[e.second * gl.numProp];
            edgeVerts.push_back({pa[0], pa[1], pa[2], blackAbgr});
            edgeVerts.push_back({pb[0], pb[1], pb[2], blackAbgr});
            const uint16_t base = uint16_t(edgeVerts.size() - 2);
            edgeIdx.push_back(base);
            edgeIdx.push_back(uint16_t(base + 1));
        }

        m_edgeVbh = bgfx::createVertexBuffer(
            bgfx::copy(edgeVerts.data(), uint32_t(edgeVerts.size() * sizeof(Vertex))),
            m_layout
        );
        m_edgeIbh = bgfx::createIndexBuffer(
            bgfx::copy(edgeIdx.data(), uint32_t(edgeIdx.size() * sizeof(uint16_t)))
        );
        m_edgeIndexCount = uint32_t(edgeIdx.size());
    }

    // ---- Textured program for the face labels -----------------------------
    const uint8_t* vsData = nullptr;
    uint32_t       vsSize = 0;
    const uint8_t* fsData = nullptr;
    uint32_t       fsSize = 0;
    switch (bgfx::getRendererType())
    {
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12:
            vsData = vs_textured_dx11; vsSize = sizeof(vs_textured_dx11);
            fsData = fs_textured_dx11; fsSize = sizeof(fs_textured_dx11);
            break;
        case bgfx::RendererType::Vulkan:
            vsData = vs_textured_vk;  vsSize = sizeof(vs_textured_vk);
            fsData = fs_textured_vk;  fsSize = sizeof(fs_textured_vk);
            break;
        default: break;
    }

    if (vsData == nullptr || fsData == nullptr)
        return;

    bgfx::ShaderHandle vs = bgfx::createShader(bgfx::copy(vsData, vsSize));
    bgfx::ShaderHandle fs = bgfx::createShader(bgfx::copy(fsData, fsSize));
    m_textProgram = bgfx::createProgram(vs, fs, true);
    m_texSampler  = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    // ---- Text atlas: 3 columns x 2 rows, each cell 256x256 ----------------
    const int kCell  = 256;
    const int kCols  = 3;
    const int kRows  = 2;
    const int kAtlasW = kCell * kCols;
    const int kAtlasH = kCell * kRows;

    QImage atlas(kAtlasW, kAtlasH, QImage::Format_RGBA8888);
    const QColor bgColor(kCubeR, kCubeG, kCubeB);
    atlas.fill(bgColor);

    {
        QPainter painter(&atlas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        QFont font("Arial", 96, QFont::Bold);
        painter.setFont(font);
        painter.setPen(QColor(40, 40, 48));

        for (const FaceDef& face : kFaces)
        {
            const int x = face.atlasCol * kCell;
            const int y = face.atlasRow * kCell;
            const QRect cell(x, y, kCell, kCell);
            painter.drawText(cell, Qt::AlignCenter, QString::fromUtf8(face.label));
        }
        painter.end();
    }

    // QImage stores RGBA8888 as R,G,B,A bytes in memory on little-endian,
    // which matches bgfx::TextureFormat::RGBA8.
    m_texture = bgfx::createTexture2D(
        uint16_t(kAtlasW),
        uint16_t(kAtlasH),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        0,
        bgfx::copy(atlas.constBits(), uint32_t(atlas.sizeInBytes()))
    );

    // ---- Text quads: one quad per face, slightly outside the face ---------
    struct TextVertex
    {
        float x, y, z;
        float u, v;
    };

    std::vector<TextVertex> textVerts;
    std::vector<uint16_t>   textIdx;

    // Half-size of the text quad. The chamfer leaves a flat central region of
    // half-size (1 - kChamfer) = 0.6 on each face; 0.55 fits inside it.
    const float half = 0.55f;
    // Push the quad just outside the face to avoid z-fighting.
    const float push = 1.01f;

    for (const FaceDef& face : kFaces)
    {
        const bx::Vec3 c = {
            face.center.x * push,
            face.center.y * push,
            face.center.z * push
        };

        const uint32_t base = uint32_t(textVerts.size());

        // UV cell. bgfx V=0 is the bottom of the texture; QImage row 0 is the
        // top, so invert V: v0 = 1 - (row+1)/rows, v1 = 1 - row/rows.
        const float u0 = float(face.atlasCol)     / float(kCols);
        const float u1 = float(face.atlasCol + 1) / float(kCols);
        const float v0 = 1.0f - float(face.atlasRow + 1) / float(kRows);
        const float v1 = 1.0f - float(face.atlasRow)     / float(kRows);

        // bottom-left
        textVerts.push_back({
            c.x - face.right.x * half - face.up.x * half,
            c.y - face.right.y * half - face.up.y * half,
            c.z - face.right.z * half - face.up.z * half,
            u0, v0
        });
        // bottom-right
        textVerts.push_back({
            c.x + face.right.x * half - face.up.x * half,
            c.y + face.right.y * half - face.up.y * half,
            c.z + face.right.z * half - face.up.z * half,
            u1, v0
        });
        // top-right
        textVerts.push_back({
            c.x + face.right.x * half + face.up.x * half,
            c.y + face.right.y * half + face.up.y * half,
            c.z + face.right.z * half + face.up.z * half,
            u1, v1
        });
        // top-left
        textVerts.push_back({
            c.x - face.right.x * half + face.up.x * half,
            c.y - face.right.y * half + face.up.y * half,
            c.z - face.right.z * half + face.up.z * half,
            u0, v1
        });

        textIdx.insert(textIdx.end(), {
            uint16_t(base + 0), uint16_t(base + 1), uint16_t(base + 2),
            uint16_t(base + 0), uint16_t(base + 2), uint16_t(base + 3)
        });
    }

    m_textLayout
        .begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    m_textVbh = bgfx::createVertexBuffer(
        bgfx::copy(textVerts.data(), uint32_t(textVerts.size() * sizeof(TextVertex))),
        m_textLayout
    );

    m_textIbh = bgfx::createIndexBuffer(
        bgfx::copy(textIdx.data(), uint32_t(textIdx.size() * sizeof(uint16_t)))
    );

    m_textIndexCount = uint32_t(textIdx.size());
    m_initialized = true;
}

void NavigationCube::destroy()
{
    if (!m_initialized)
        return;

    if (bgfx::isValid(m_vbh)) bgfx::destroy(m_vbh);
    if (bgfx::isValid(m_ibh)) bgfx::destroy(m_ibh);
    if (bgfx::isValid(m_edgeVbh)) bgfx::destroy(m_edgeVbh);
    if (bgfx::isValid(m_edgeIbh)) bgfx::destroy(m_edgeIbh);
    if (bgfx::isValid(m_textProgram)) bgfx::destroy(m_textProgram);
    if (bgfx::isValid(m_texture))     bgfx::destroy(m_texture);
    if (bgfx::isValid(m_texSampler))  bgfx::destroy(m_texSampler);
    if (bgfx::isValid(m_textVbh))     bgfx::destroy(m_textVbh);
    if (bgfx::isValid(m_textIbh))     bgfx::destroy(m_textIbh);

    m_vbh         = BGFX_INVALID_HANDLE;
    m_ibh         = BGFX_INVALID_HANDLE;
    m_edgeVbh     = BGFX_INVALID_HANDLE;
    m_edgeIbh     = BGFX_INVALID_HANDLE;
    m_textProgram = BGFX_INVALID_HANDLE;
    m_texture     = BGFX_INVALID_HANDLE;
    m_texSampler  = BGFX_INVALID_HANDLE;
    m_textVbh     = BGFX_INVALID_HANDLE;
    m_textIbh     = BGFX_INVALID_HANDLE;
    m_initialized = false;
}

void NavigationCube::render(uint8_t view,
                            float    yaw,
                            float    pitch,
                            uint16_t frameWidth,
                            uint16_t frameHeight)
{
    (void)frameWidth;
    (void)frameHeight;
    if (!m_initialized)
        return;

    m_yaw   = yaw;
    m_pitch = pitch;

    const uint16_t vpX = kMargin;
    const uint16_t vpY = kMargin;
    const uint16_t vpW = kSize;
    const uint16_t vpH = kSize;

    bgfx::setViewRect(view, vpX, vpY, vpW, vpH);
    bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);

    const bx::Vec3 at  = {0.0f, 0.0f, 0.0f};
    const bx::Vec3 eye = {0.0f, 0.0f, -kCamDist};

    float viewMtx[16];
    bx::mtxLookAt(viewMtx, eye, at);

    float projMtx[16];
    bx::mtxOrtho(
        projMtx,
        -kOrthoHalf,  kOrthoHalf,
        -kOrthoHalf,  kOrthoHalf,
        0.1f, 100.0f,
        0.0f,
        bgfx::getCaps()->homogeneousDepth
    );

    bgfx::setViewTransform(view, viewMtx, projMtx);

    // Orient the cube so it shows the same view of the world axes as the main
    // camera.  The scene camera orbits with eye = {sin(yaw)cos(pitch),
    // sin(pitch), -cos(yaw)cos(pitch)}; applying Ry(-yaw) * Rx(+pitch) to the
    // cube reproduces that orientation (verified at the yaw/pitch poles).
    float ry[16];
    float rx[16];
    float model[16];
    bx::mtxRotateY(ry, -yaw);
    bx::mtxRotateX(rx,  pitch);
    bx::mtxMul(model, rx, ry);

    // 1) Solid chamfered cube (single color).
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh, 0, m_indexCount);
    bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
    bgfx::submit(view, m_program);

    // 2) Black wireframe edges (line list). Drawn on top of the faces with
    // depth test LEQUAL so they show through without z-fighting.
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, m_edgeVbh);
    bgfx::setIndexBuffer(m_edgeIbh, 0, m_edgeIndexCount);
    bgfx::setState(
        (BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK & ~BGFX_STATE_WRITE_Z
                              & ~BGFX_STATE_DEPTH_TEST_MASK)
        | BGFX_STATE_DEPTH_TEST_LEQUAL
        | BGFX_STATE_PT_LINES
    );
    bgfx::submit(view, m_program);

    // 3) Text labels on each face.
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, m_textVbh);
    bgfx::setIndexBuffer(m_textIbh, 0, m_textIndexCount);
    bgfx::setTexture(0, m_texSampler, m_texture);
    bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
    bgfx::submit(view, m_textProgram);
}

bool NavigationCube::hitTest(int mouseX, int mouseY,
                             uint16_t /*frameWidth*/, uint16_t /*frameHeight*/,
                             int& outFace) const
{
    if (!m_initialized)
        return false;

    if (mouseX < int(kMargin) || mouseY < int(kMargin) ||
        mouseX >= int(kMargin + kSize) || mouseY >= int(kMargin + kSize))
        return false;

    const float lx = float(mouseX - int(kMargin));
    const float ly = float(mouseY - int(kMargin));
    const float nx = (lx / float(kSize)) * 2.0f - 1.0f;
    const float ny = 1.0f - (ly / float(kSize)) * 2.0f;

    const bx::Vec3 rayOrigin = {nx * kOrthoHalf, ny * kOrthoHalf, -kCamDist};
    const bx::Vec3 rayDir    = {0.0f, 0.0f, 1.0f};

    // Inverse cube rotation: model = Rx(+pitch) * Ry(-yaw), so
    // R^-1 = Ry(+yaw) * Rx(-pitch).
    float rxi[16];
    float ryi[16];
    float invRot[16];
    bx::mtxRotateX(rxi, -m_pitch);
    bx::mtxRotateY(ryi,  m_yaw);
    bx::mtxMul(invRot, ryi, rxi);

    auto transformDir = [&](const bx::Vec3& v) -> bx::Vec3
    {
        return {
            invRot[0] * v.x + invRot[4] * v.y + invRot[8]  * v.z,
            invRot[1] * v.x + invRot[5] * v.y + invRot[9]  * v.z,
            invRot[2] * v.x + invRot[6] * v.y + invRot[10] * v.z,
        };
    };
    auto transformPoint = [&](const bx::Vec3& v) -> bx::Vec3
    {
        return {
            invRot[0] * v.x + invRot[4] * v.y + invRot[8]  * v.z + invRot[12],
            invRot[1] * v.x + invRot[5] * v.y + invRot[9]  * v.z + invRot[13],
            invRot[2] * v.x + invRot[6] * v.y + invRot[10] * v.z + invRot[14],
        };
    };

    const bx::Vec3 localOrigin = transformPoint(rayOrigin);
    const bx::Vec3 localDir    = bx::normalize(transformDir(rayDir));

    const float minB[3] = {-1.0f, -1.0f, -1.0f};
    const float maxB[3] = { 1.0f,  1.0f,  1.0f};
    const float o[3]    = {localOrigin.x, localOrigin.y, localOrigin.z};
    const float d[3]    = {localDir.x,    localDir.y,    localDir.z};

    float tmin = -1e30f;
    float tmax =  1e30f;
    int   hitAxis = 0;
    int   hitSign = 0;

    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::fabs(d[axis]) < 1e-8f)
        {
            if (o[axis] < minB[axis] || o[axis] > maxB[axis])
                return false;
        }
        else
        {
            const float inv = 1.0f / d[axis];
            float t1 = (minB[axis] - o[axis]) * inv;
            float t2 = (maxB[axis] - o[axis]) * inv;
            int sign = (d[axis] < 0.0f) ? 1 : -1;

            if (t1 > t2) { std::swap(t1, t2); sign = -sign; }

            if (t1 > tmin) { tmin = t1; hitAxis = axis; hitSign = sign; }
            if (t2 < tmax) { tmax = t2; }

            if (tmin > tmax)
                return false;
        }
    }

    if (tmin < 0.0f)
        return false;

    outFace = hitAxis * 2 + (hitSign > 0 ? 1 : 0);
    return true;
}
