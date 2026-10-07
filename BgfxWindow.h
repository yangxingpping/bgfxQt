#pragma once

#include <QWidget>
#include <QPoint>

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <manifold/manifold.h>

#include <cstdint>
#include <vector>

#include "NavigationCube.h"

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
	void drawModel();
    void drawModelWithTransient();
    void drawAxis3D();
    void drawLight();
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

    // Model mesh (uploaded from m_manifold)
    bgfx::VertexLayout       m_modelLayout;
    bgfx::VertexBufferHandle m_modelVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_modelIbh = BGFX_INVALID_HANDLE;
    uint32_t                 m_modelIndexCount = 0;
    bool                     m_modelBuilt = false;

    // Transient-buffer draw path: the packed mesh is cached on the CPU once
    // and copied into bgfx's per-frame transient pools on every draw, so no
    // persistent GPU buffers are created or destroyed.
    std::vector<uint8_t>     m_transientVertexData;   // packed ModelVertex bytes
    std::vector<uint32_t>    m_transientIndexData;    // 32-bit indices
    uint32_t                 m_transientVertexCount = 0;
    bool                     m_transientBuilt = false;

    // 3D axis gizmo (XYZ) drawn at the orbit target.
    bgfx::VertexLayout       m_axisLayout;
    bgfx::VertexBufferHandle m_axisVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_axisIbh = BGFX_INVALID_HANDLE;
    uint32_t                 m_axisIndexCount = 0;
    bool                     m_axisBuilt = false;

    // Daylight point-light gizmo (small emissive sphere at m_lightPos).
    bgfx::VertexLayout       m_lightLayout;
    bgfx::VertexBufferHandle m_lightVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_lightIbh = BGFX_INVALID_HANDLE;
    uint32_t                 m_lightIndexCount = 0;
    bool                     m_lightBuilt = false;
    bx::Vec3                 m_lightPos = {50.0f, 50.0f, 50.0f};

    // Orbit camera
    bool  m_leftDragging   = false;
    bool  m_rightDragging  = false;
    QPoint m_lastMousePos;
    bx::Vec3 m_target      = {0.0f, 0.0f, 0.0f};
    float m_cameraDistance = 5.0f;
    float m_cameraYaw      = 0.0f;   // radians, around Y
    float m_cameraPitch    = 0.0f;   // radians

	manifold::Manifold m_manifold;
	manifold::MeshGL m_mesh;

    NavigationCube m_navCube;
};
