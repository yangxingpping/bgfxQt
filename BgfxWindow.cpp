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

    if (m_cubeInitialized)
    {
        float model[16];
        bx::mtxIdentity(model);

        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, m_vbh);
        bgfx::setIndexBuffer(m_ibh);
        // Disable face culling so every face is drawn regardless of winding.
        bgfx::setState(BGFX_STATE_DEFAULT & ~BGFX_STATE_CULL_MASK);
        bgfx::submit(0, m_program);
    }

    bgfx::frame();
}

void BgfxWindow::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);

    const QPoint pos = event->position().toPoint();

    if (event->button() == Qt::LeftButton)
    {
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
        // Horizontal drag -> yaw, vertical drag -> pitch.
        m_cameraYaw   += float(delta.x()) * 0.005f;
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

    bgfx::shutdown();

    m_bgfxInitialized = false;
}
