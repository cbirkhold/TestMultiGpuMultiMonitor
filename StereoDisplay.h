//
//  StereoDisplay.h
//  StereoDisplay
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __STEREO_DISPLAY_H__
#define __STEREO_DISPLAY_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <array>

#include "__WindowsApi.h"

#include "_GLMApi.h"
#include "_OpenGLApi.h"
#include "_OpenVRApi.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "OpenGLUtils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ColorSpace {
    LINEAR,
    SRGB,
};

enum class EyeIndex {
    LEFT,
    RIGHT
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// A render target that holds a single shared framebuffer or one framebuffer per
// eye. If there is a single framebuffer the getters for either eye return the
// same values.
//------------------------------------------------------------------------------

class StereoRenderTarget
{
    //------------------------------------------------------------------------------
    // Configuration/Types
public:

    struct default_framebuffer_tag {};
    static constexpr default_framebuffer_tag DEFAULT_FRAMEBUFFER_TAG{};

    struct create_tag {};
    static constexpr create_tag CREATE_TAG{};

    struct create_single_framebuffer_tag {};
    static constexpr create_single_framebuffer_tag CREATE_SINGLE_FRAMEBUFFER_TAG{};

    static constexpr size_t NUM_EYES = 2;

    //------------------------------------------------------------------------------
    // Construction/Destruction
public:

    StereoRenderTarget(size_t width, size_t height, ColorSpace color_space, const default_framebuffer_tag&);
    StereoRenderTarget(size_t width, size_t height, ColorSpace color_space, const create_tag&);
    StereoRenderTarget(size_t width, size_t height, ColorSpace color_space, const create_single_framebuffer_tag&);
    ~StereoRenderTarget();

    //------------------------------------------------------------------------------
    // Framebuffers
public:

    bool default_framebuffer() const noexcept { return (m_framebuffers[0] == 0); }
    bool single_framebuffer() const noexcept { return (m_framebuffers[0] == m_framebuffers[1]); }

    size_t width() const noexcept { return m_width; }
    size_t height() const noexcept { return m_height; }
    ColorSpace color_space() const noexcept { return m_color_space; }

    GLuint framebuffer(size_t eye_index) const { return m_framebuffers[eye_index]; }
    GLuint color_attachment(size_t eye_index) const { return m_color_attachments[eye_index]; }

    glm::ivec4 viewport(size_t eye_index) const { return m_viewports[eye_index]; }
    glm::vec4 bounds(size_t eye_index) const { return m_bounds[eye_index]; }

    void bind_single_framebuffer() const;
    void bind_eye(size_t eye_index) const;
    void unbind_eye() const;

    //------------------------------------------------------------------------------
    // {Private}
private:

    const size_t                            m_width;                    // Width of the framebuffers
    const size_t                            m_height;                   // Height of the framebuffers
    const ColorSpace                        m_color_space;              // Color space of the framebuffer color attachments

    std::array<GLuint, NUM_EYES>            m_framebuffers = {};        // Framebuffers
    std::array<GLuint, NUM_EYES>            m_color_attachments = {};   // Framebuffer color attachments

    std::array<glm::ivec4, NUM_EYES>        m_viewports = {};           // Viewports into the framebuffers [pixels]
    std::array<glm::vec4, NUM_EYES>         m_bounds = {};              // Viewports into the framebuffers [normalized]

    void validate_initializer_list();
    void init_single_framebuffer() noexcept;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Minimal interface to a stereo display.
//------------------------------------------------------------------------------

class StereoDisplay
{
    //------------------------------------------------------------------------------
    // Construction/Destruction
protected:

    StereoDisplay(std::pair<HDC, HGLRC> context);

    HDC display_context() const noexcept { return m_display_context; }
    HGLRC opengl_context() const noexcept { return m_opengl_context; }

public:

    virtual ~StereoDisplay() {}

    //------------------------------------------------------------------------------
    // Interface
public:

    virtual glm::mat4 projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept = 0;

    void make_current() const;
    virtual const StereoRenderTarget& render_target() const noexcept = 0;

    virtual void submit() const = 0;
    virtual void render(const StereoDisplay& stereo_display, double timestamp) const = 0;

    //-----------------------------------------------------------------------------
    // {Private}
private:

    const HDC       m_display_context;
    const HGLRC     m_opengl_context;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Minimal interface to a pose tracker (such as an HMD and its controllers).
//
// Enables waiting for the next display pose to become available. This would
// usually be called right before requesting the next drawable to obtain the
// most accurate display pose for rendering the next frame.
//------------------------------------------------------------------------------

class PoseTracker
{
    //------------------------------------------------------------------------------
    // Construction/Destruction
public:

    virtual ~PoseTracker() {}

protected:

    PoseTracker() {}

    //------------------------------------------------------------------------------
    // Interface
public:

    virtual void wait_get_poses() = 0;
    virtual glm::mat4 hmd_pose() const noexcept = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class WindowStereoDisplay : public StereoDisplay
{
    //------------------------------------------------------------------------------
    // Constants/Types
public:

    static constexpr size_t NUM_EYES = 2;

    //------------------------------------------------------------------------------
    // Construction/Destruction
public:

    WindowStereoDisplay(std::pair<HDC, HGLRC> context, size_t width, size_t height, ColorSpace color_space, double fov, double ipd);

    //------------------------------------------------------------------------------
    // [StereoDisplay]
public:

    glm::mat4 projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept override;

    const StereoRenderTarget& render_target() const noexcept override { return m_render_target; }

    void submit() const override;
    void render(const StereoDisplay& stereo_display, double timestamp) const override;

    //------------------------------------------------------------------------------
    // {Private}
private:

    const StereoRenderTarget        m_render_target;

    double                          m_fov;
    double                          m_ipd;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class OpenVRStereoDisplay : public StereoDisplay
{
    //------------------------------------------------------------------------------
    // Constants/Types
public:

    struct create_single_framebuffer_tag {};
    static constexpr create_single_framebuffer_tag CREATE_SINGLE_FRAMEBUFFER_TAG{};

    static constexpr size_t NUM_EYES = 2;
    static constexpr bool FAIL_IF_WATCHDOG_EXPIRES = false;

    //------------------------------------------------------------------------------
    // Construction/Destruction
public:

    OpenVRStereoDisplay(std::pair<HDC, HGLRC> context, vr::EVRSubmitFlags submit_flags, size_t width, size_t height, ColorSpace color_space);
    OpenVRStereoDisplay(std::pair<HDC, HGLRC> context, vr::EVRSubmitFlags submit_flags, size_t width, size_t height, ColorSpace color_space, const create_single_framebuffer_tag&);

    //------------------------------------------------------------------------------
    // [StereoDisplay]
public:

    glm::mat4 projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept override;

    const StereoRenderTarget& render_target() const noexcept override { return m_render_target; }

    void submit() const override;
    void render(const StereoDisplay& stereo_display, double timestamp) const override;

    //------------------------------------------------------------------------------
    // {Private}
private:

    const StereoRenderTarget                            m_render_target;

    vr::IVRSystem* const                                m_system;
    vr::IVRCompositor* const                            m_compositor;
    const vr::EVRSubmitFlags                            m_submit_flags;

    std::array<vr::Texture_t, NUM_EYES>                 m_submit_textures = {};
    std::array < vr::VRTextureBounds_t, NUM_EYES>       m_submit_bounds = {};

    void validate_initializer_list();
    void cache_submit_parameters();
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __STEREO_DISPLAY_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
