#pragma once

#include <QWidget>
#include <QPoint>

#include <bgfx/bgfx.h>
#include <bx/math.h>

QT_BEGIN_NAMESPACE
class QMouseEvent;
class QWheelEvent;
class QTimer;
QT_END_NAMESPACE

class BgfxWindow : public QWidget
{
    Q_OBJECT

public:
    explicit BgfxWindow(QWidget* parent = nullptr);
    ~BgfxWindow() override;

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    bool initBgfx();
    bool initCube();
    void destroyCube();
    void shutdownBgfx();
    void renderFrame();
    QSize physicalSize() const;

private:
    bool m_bgfxInitialized = false;
    bool m_cubeInitialized = false;

    QTimer* m_renderTimer = nullptr;

    bgfx::VertexLayout  m_layout;
    bgfx::VertexBufferHandle m_vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_ibh = BGFX_INVALID_HANDLE;
    bgfx::ShaderHandle m_vertexShader = BGFX_INVALID_HANDLE;
    bgfx::ShaderHandle m_fragmentShader = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;

    int64_t m_timeOffset = 0;

    // Orbit camera
    bool  m_leftDragging   = false;
    bool  m_rightDragging  = false;
    QPoint m_lastMousePos;
    bx::Vec3 m_target      = {0.0f, 0.0f, 0.0f};
    float m_cameraDistance = 5.0f;
    float m_cameraYaw      = 0.0f;   // radians, around Y
    float m_cameraPitch    = 0.0f;   // radians
};
