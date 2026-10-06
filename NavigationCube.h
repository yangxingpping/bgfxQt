#pragma once

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <cstdint>

/// FreeCAD-style navigation cube rendered as an overlay in the top-left
/// corner of the viewport. The cube rotates to match the main camera's
/// orientation, and clicking a face snaps the camera to look along that axis.
///
/// Face indices (used by hitTest):
///   0 -> +X (right)
///   1 -> -X (left)
///   2 -> +Y (top)
///   3 -> -Y (bottom)
///   4 -> +Z (front)
///   5 -> -Z (back)
class NavigationCube
{
public:
    // Viewport size and margin, in physical pixels.
    static constexpr uint16_t kSize   = 140;
    static constexpr uint16_t kMargin = 10;

    NavigationCube();
    ~NavigationCube();

    /// Create the cube GPU resources. `program` must be a compiled bgfx
    /// program accepting a_position (vec3) and a_color0 (u8vec4).
    void init(bgfx::ProgramHandle program);

    /// Release GPU resources.
    void destroy();

    /// Render the navigation cube into bgfx view `view`, oriented by the main
    /// camera's yaw/pitch. The cube is placed in the top-left corner.
    void render(uint8_t view,
                float    yaw,
                float    pitch,
                uint16_t frameWidth,
                uint16_t frameHeight);

    /// Returns true if the click (widget-local physical pixels) lands on a
    /// cube face and sets `outFace` to the face index (0..5).
    bool hitTest(int mouseX, int mouseY,
                 uint16_t frameWidth, uint16_t frameHeight,
                 int& outFace) const;

private:
    bgfx::VertexBufferHandle m_vbh     = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_ibh     = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout       m_layout;
    bgfx::ProgramHandle      m_program = BGFX_INVALID_HANDLE;
    uint32_t                 m_indexCount = 0;
    bool                     m_initialized = false;

    // Cached orientation for hit testing (matches the last render call).
    float m_yaw   = 0.0f;
    float m_pitch = 0.0f;
};
