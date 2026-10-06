#pragma once

#include <QWidget>

#include <bgfx/bgfx.h>

QT_BEGIN_NAMESPACE
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
};
