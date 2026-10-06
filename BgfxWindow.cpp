#include "BgfxWindow.h"

#include <QShowEvent>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QTimer>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

    init.resolution.width  = width();
    init.resolution.height = height();

    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init))
    {
        return false;
    }

#endif

    m_bgfxInitialized = true;

    m_renderTimer = new QTimer(this);
    connect(m_renderTimer, &QTimer::timeout, this, &BgfxWindow::renderFrame);
    m_renderTimer->start(16);

    return true;
}

void BgfxWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (!m_bgfxInitialized)
        return;

    bgfx::reset(
        event->size().width(),
        event->size().height(),
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
        0xff000000,
        1.0f,
        0
    );

    bgfx::setViewRect(
        0,
        0,
        0,
        uint16_t(width()),
        uint16_t(height())
    );

    bgfx::touch(0);

    bgfx::frame();
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

    bgfx::shutdown();

    m_bgfxInitialized = false;
}
