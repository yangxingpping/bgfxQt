#include "NavigationCube.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <manifold/manifold.h>

#include <algorithm>
#include <cmath>
#include <vector>

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

    // Build a chamfered cube: start from a unit cube [-1, 1]^3 and trim each
    // of the 8 corners with a plane. The result keeps the face centers at ±1
    // (so the bounding box is exactly [-1, 1] and hit-testing stays exact)
    // while adding flat bevels to every edge and corner.
    manifold::Manifold mesh = manifold::Manifold::Cube({2.0, 2.0, 2.0}, true);
    // TrimByPlane keeps normal·p >= originOffset.  The corner plane for the
    // (sx,sy,sz) corner is sx*x+sy*y+sz*z = 3-d; we keep the side <= 3-d, so
    // pass the inward unit normal and the negated signed distance offset.
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

    // Compute per-vertex normals for a shaded color (normal * 0.5 + 0.5).
    std::vector<bx::Vec3> normals(numVert, {0.0f, 0.0f, 0.0f});
    for (uint32_t t = 0; t < numTri; ++t)
    {
        const uint32_t i0 = gl.triVerts[t * 3 + 0];
        const uint32_t i1 = gl.triVerts[t * 3 + 1];
        const uint32_t i2 = gl.triVerts[t * 3 + 2];

        const float* p0 = &gl.vertProperties[i0 * gl.numProp];
        const float* p1 = &gl.vertProperties[i1 * gl.numProp];
        const float* p2 = &gl.vertProperties[i2 * gl.numProp];

        const bx::Vec3 e1 = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const bx::Vec3 e2 = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        bx::Vec3 n = bx::cross(e1, e2);
        const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
        if (len2 > 1e-12f)
        {
            const float inv = 1.0f / bx::sqrt(len2);
            n = {n.x * inv, n.y * inv, n.z * inv};
        }
        else
        {
            n = {0.0f, 1.0f, 0.0f};
        }
        normals[i0] = bx::add(normals[i0], n);
        normals[i1] = bx::add(normals[i1], n);
        normals[i2] = bx::add(normals[i2], n);
    }
    for (uint32_t i = 0; i < numVert; ++i)
    {
        bx::Vec3& n = normals[i];
        const float len2 = n.x * n.x + n.y * n.y + n.z * n.z;
        if (len2 > 1e-12f)
        {
            const float inv = 1.0f / bx::sqrt(len2);
            n = {n.x * inv, n.y * inv, n.z * inv};
        }
        else
        {
            n = {0.0f, 1.0f, 0.0f};
        }
    }

    struct Vertex
    {
        float    x, y, z;
        uint32_t abgr;
    };

    std::vector<Vertex> vertices(numVert);
    for (uint32_t i = 0; i < numVert; ++i)
    {
        const float* p = &gl.vertProperties[i * gl.numProp];
        const bx::Vec3& n = normals[i];

        const uint8_t r = uint8_t(bx::clamp(n.x * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
        const uint8_t g = uint8_t(bx::clamp(n.y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
        const uint8_t b = uint8_t(bx::clamp(n.z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);

        vertices[i].x = p[0];
        vertices[i].y = p[1];
        vertices[i].z = p[2];
        vertices[i].abgr =
            (uint32_t(255) << 24) |
            (uint32_t(b)   << 16) |
            (uint32_t(g)   << 8)  |
             uint32_t(r);
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
    m_initialized = true;
}

void NavigationCube::destroy()
{
    if (!m_initialized)
        return;

    if (bgfx::isValid(m_vbh)) bgfx::destroy(m_vbh);
    if (bgfx::isValid(m_ibh)) bgfx::destroy(m_ibh);

    m_vbh = BGFX_INVALID_HANDLE;
    m_ibh = BGFX_INVALID_HANDLE;
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

    // Orient the cube with the inverse of the camera orbit: model = Rx(-pitch) * Ry(-yaw).
    float ry[16];
    float rx[16];
    float model[16];
    bx::mtxRotateY(ry, -yaw);
    bx::mtxRotateX(rx, -pitch);
    bx::mtxMul(model, rx, ry);

    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh, 0, m_indexCount);
    bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
    bgfx::submit(view, m_program);
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

    // Inverse cube rotation: R^-1 = Ry(yaw) * Rx(pitch).
    float rxi[16];
    float ryi[16];
    float invRot[16];
    bx::mtxRotateX(rxi, m_pitch);
    bx::mtxRotateY(ryi, m_yaw);
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
