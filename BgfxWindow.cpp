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

#include <vector>

#include "vs_cube_dx11.bin.h"
#include "fs_cube_dx11.bin.h"
#include "vs_cube_vk.bin.h"
#include "fs_cube_vk.bin.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{

struct PosColorVertex
{
    float    x;
    float    y;
    float    z;
    uint32_t abgr; // RGBA8, little-endian byte order R, G, B, A
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


void BgfxWindow::drawModel()
{
    if (m_manifold.IsEmpty())
        return;

    // Upload the manifold mesh to GPU buffers once (lazy).
    if (!m_modelBuilt)
    {
        const manifold::MeshGL mesh = m_manifold.GetMeshGL();
        const uint32_t numVert = uint32_t(mesh.NumVert());
        const uint32_t numTri  = uint32_t(mesh.NumTri());

        if (numVert == 0 || numTri == 0)
            return;

        // Compute per-vertex normals by averaging face normals.
        std::vector<bx::Vec3> normals(numVert, {0.0f, 0.0f, 0.0f});
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

        // Pack position (3 floats) + color (RGBA8) into a bgfx vertex buffer.
        struct ModelVertex
        {
            float    x, y, z;
            uint32_t abgr;
        };

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
                (uint32_t(g) << 8)  |
                 uint32_t(r);
        }

        m_modelLayout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
            .end();

        m_modelVbh = bgfx::createVertexBuffer(
            bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(ModelVertex))),
            m_modelLayout
        );

        m_modelIbh = bgfx::createIndexBuffer(
            bgfx::copy(mesh.triVerts.data(), uint32_t(mesh.triVerts.size() * sizeof(uint32_t))),
            BGFX_BUFFER_INDEX32
        );

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

    m_bgfxInitialized = true;

    if (!initCube())
        return false;

    // Navigation cube shares the same position+color shader as the cube.
    m_navCube.init(m_program);

    // Sample manifold model rendered by drawModel().
    m_manifold = manifold::Manifold::Cube(manifold::vec3(1.0f, 2.0f, 4.0f));

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

    float proj[16];
    bx::mtxProj(
        proj,
        60.0f,
        float(fbSize.width()) / float(fbSize.height()),
        0.1f,
        100.0f,
        bgfx::getCaps()->homogeneousDepth
    );

    bgfx::setViewTransform(0, view, proj);

    bgfx::touch(0);

    drawModel();

    // Render the navigation cube overlay in the top-left corner (view 1).
    m_navCube.render(1, m_cameraYaw, m_cameraPitch,
                     uint16_t(fbSize.width()), uint16_t(fbSize.height()));

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
            // Snap the camera to look along the clicked face's axis.
            // Face: 0:+X, 1:-X, 2:+Y, 3:-Y, 4:+Z, 5:-Z
            switch (face)
            {
                case 0: m_cameraYaw = -bx::kPi * 0.5f; m_cameraPitch = 0.0f; break;
                case 1: m_cameraYaw =  bx::kPi * 0.5f; m_cameraPitch = 0.0f; break;
                case 2: m_cameraYaw = 0.0f;            m_cameraPitch =  bx::kPi * 0.5f; break;
                case 3: m_cameraYaw = 0.0f;            m_cameraPitch = -bx::kPi * 0.5f; break;
                case 4: m_cameraYaw = 0.0f;            m_cameraPitch = 0.0f; break;
                case 5: m_cameraYaw = bx::kPi;         m_cameraPitch = 0.0f; break;
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

    bgfx::shutdown();

    m_bgfxInitialized = false;
}
