#include "NavigationCube.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>

namespace
{

// Per-vertex colored cube (8 vertices, 12 triangles). The colors make each
// face visually distinct so the orientation is easy to read.
struct NavVertex
{
    float    x;
    float    y;
    float    z;
    uint32_t abgr; // RGBA8, little-endian byte order R, G, B, A
};

static const NavVertex kVertices[] =
{
    {-1.0f,  1.0f,  1.0f, 0xff000000}, // 0
    { 1.0f,  1.0f,  1.0f, 0xff0000ff}, // 1  +X red
    {-1.0f, -1.0f,  1.0f, 0xff00ff00}, // 2
    { 1.0f, -1.0f,  1.0f, 0xff00ffff}, // 3
    {-1.0f,  1.0f, -1.0f, 0xffff0000}, // 4  +Z blue
    { 1.0f,  1.0f, -1.0f, 0xffff00ff}, // 5
    {-1.0f, -1.0f, -1.0f, 0xffffff00}, // 6  +Y yellow
    { 1.0f, -1.0f, -1.0f, 0xffffffff}, // 7
};

static const uint16_t kIndices[] =
{
    0, 1, 2,  1, 3, 2,  // +Z (front)
    4, 6, 5,  5, 6, 7,  // -Z (back)
    0, 2, 4,  4, 2, 6,  // -X (left)
    1, 5, 3,  5, 7, 3,  // +X (right)
    0, 4, 1,  4, 5, 1,  // +Y (top)
    2, 3, 6,  6, 3, 7,  // -Y (bottom)
};

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

    m_layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
        .end();

    m_vbh = bgfx::createVertexBuffer(
        bgfx::makeRef(kVertices, sizeof(kVertices)),
        m_layout
    );

    m_ibh = bgfx::createIndexBuffer(
        bgfx::makeRef(kIndices, sizeof(kIndices))
    );

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
    if (!m_initialized)
        return;

    // Cache orientation for hit testing.
    m_yaw   = yaw;
    m_pitch = pitch;

    // Place the cube in the top-left corner.
    const uint16_t vpX = kMargin;
    const uint16_t vpY = kMargin;
    const uint16_t vpW = kSize;
    const uint16_t vpH = kSize;

    bgfx::setViewRect(view, vpX, vpY, vpW, vpH);

    // Clear only the depth buffer in this viewport so the cube composites
    // over the main scene's color output.
    bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);

    // Orthographic camera looking at the cube from -Z.
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

    // Orient the cube with the inverse of the camera orbit so it reflects
    // the current viewing direction: model = Rx(-pitch) * Ry(-yaw).
    float ry[16];
    float rx[16];
    float model[16];
    bx::mtxRotateY(ry, -yaw);
    bx::mtxRotateX(rx, -pitch);
    bx::mtxMul(model, rx, ry);

    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    // Depth test on, RGB write on, no face culling.
    bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
    bgfx::submit(view, m_program);
}

bool NavigationCube::hitTest(int mouseX, int mouseY,
                             uint16_t /*frameWidth*/, uint16_t /*frameHeight*/,
                             int& outFace) const
{
    if (!m_initialized)
        return false;

    // Is the click inside the nav cube viewport?
    if (mouseX < int(kMargin) || mouseY < int(kMargin) ||
        mouseX >= int(kMargin + kSize) || mouseY >= int(kMargin + kSize))
        return false;

    // Convert click to orthographic view-space coordinates.
    const float lx = float(mouseX - int(kMargin));
    const float ly = float(mouseY - int(kMargin));
    const float nx = (lx / float(kSize)) * 2.0f - 1.0f; // [-1, 1]
    const float ny = 1.0f - (ly / float(kSize)) * 2.0f; // flip Y

    // Orthographic ray in view space: origin on the near plane, dir +Z.
    const bx::Vec3 rayOrigin = {nx * kOrthoHalf, ny * kOrthoHalf, -kCamDist};
    const bx::Vec3 rayDir    = {0.0f, 0.0f, 1.0f};

    // Transform the ray into cube-local space using the inverse of the cube
    // rotation. R = Rx(-pitch) * Ry(-yaw), so R^-1 = Ry(yaw) * Rx(pitch).
    float rxi[16];
    float ryi[16];
    float invRot[16];
    bx::mtxRotateX(rxi, m_pitch);
    bx::mtxRotateY(ryi, m_yaw);
    bx::mtxMul(invRot, ryi, rxi); // Ry(yaw) * Rx(pitch)

    // Transform direction (no translation) and origin (with translation=0).
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

    // Slab intersection against the unit cube [-1, 1]^3.
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
            int sign = (d[axis] < 0.0f) ? 1 : -1; // which face normal we hit

            if (t1 > t2) { std::swap(t1, t2); sign = -sign; }

            if (t1 > tmin) { tmin = t1; hitAxis = axis; hitSign = sign; }
            if (t2 < tmax) { tmax = t2; }

            if (tmin > tmax)
                return false;
        }
    }

    if (tmin < 0.0f)
        return false;

    // Map (axis, sign) to face index:
    //   axis 0 (X): sign -1 -> +X face (0), sign +1 -> -X face (1)
    //   axis 1 (Y): sign -1 -> +Y face (2), sign +1 -> -Y face (3)
    //   axis 2 (Z): sign -1 -> +Z face (4), sign +1 -> -Z face (5)
    outFace = hitAxis * 2 + (hitSign > 0 ? 1 : 0);
    return true;
}
