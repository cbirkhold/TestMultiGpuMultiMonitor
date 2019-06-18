//
//  Wrapper.cpp
//  StereoDisplay
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Wrapper.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <array>
#include <sstream>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "../../OpenGLUtils.h"
#include "../../OpenVRUtils.h"
#include "../../Utils.h"
#include "../../Watchdog.h"

#include "HWWrapper/include/HWWrapper.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

    constexpr size_t NUM_EYES = 2;

    vr::VRTextureBounds_t make_texture_bounds(float min_x, float min_y, float max_x, float max_y)
    {
        vr::VRTextureBounds_t bounds;

        bounds.uMin = min_x;
        bounds.vMin = min_y;
        bounds.uMax = max_x;
        bounds.vMax = max_y;

        return bounds;
    }

    vr::VRTextureBounds_t make_texture_bounds(glm::vec4 bounds_)
    {
        vr::VRTextureBounds_t bounds;

        bounds.uMin = bounds_.x;
        bounds.vMin = bounds_.y;
        bounds.uMax = bounds_.z;
        bounds.vMax = bounds_.w;

        return bounds;
    }

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Shared data between the display and the internal drawable implementations.
//------------------------------------------------------------------------------

class WrapperDisplay::RenderTarget : public AcquireReleaseWithOwnership
{
    //------------------------------------------------------------------------------
     // Construction/Destruction
public:

    RenderTarget()
        : m_width(0)
        , m_height(0)
    {
    }

    RenderTarget(size_t width, size_t height/*, bool srgb*/)
        : m_width(width)
        , m_height(height)
    {
        if ((width == 0) || (height == 0)) {
            throw std::runtime_error("Valid render target size expected!");
        }

        toolbox::OpenGLFramebuffer::create_texture_backed(m_framebuffers.data(), m_color_attachments.data(), nullptr, 2, m_width, m_height);
    }

    ~RenderTarget()
    {
        if (valid()) {
            toolbox::OpenGLFramebuffer::delete_texture_backed(m_framebuffers.data(), m_color_attachments.data(), nullptr, 2);
        }
    }

    //------------------------------------------------------------------------------
    // Framebuffers
public:

    bool valid() const noexcept { return (m_width > 0 ? true : false); }

    size_t width() const noexcept { return m_width; }
    size_t height() const noexcept { return m_height; }

    GLuint framebuffer(size_t eye_index) const { return m_framebuffers[eye_index]; }
    GLuint color_attachment(size_t eye_index) const { return m_color_attachments[eye_index]; }

    //------------------------------------------------------------------------------
    // {Private}
private:

    const size_t                    m_width;                    // Width of the framebuffers
    const size_t                    m_height;                   // Height of the framebuffers

    std::array<GLuint, 2>           m_framebuffers = {};        // Framebuffers
    std::array<GLuint, 2>           m_color_attachments = {};   // Framebuffer color attachments
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Wrapper implementation of StereoDrawable.
//------------------------------------------------------------------------------

class WrapperDisplay::Drawable : public StereoDrawable
{
    //------------------------------------------------------------------------------
    // Construction/Destruction
public:

    //------------------------------------------------------------------------------
    // Create the drawable and acquire the given render target until submit() is
    // called at which point the render target is released, success or not.
    Drawable(
        std::shared_ptr<HWW::HWWrapper> wrapper,
        std::shared_ptr<const RenderTarget> render_target,
        double audio_timestamp
    )
        : m_wrapper(std::move(wrapper))
        , m_render_target(std::move(render_target))
        , m_audio_timestamp(audio_timestamp)
    {
        m_viewport = glm::ivec4(0, 0, m_render_target->width(), m_render_target->height());

        if (!m_wrapper) {
            throw std::runtime_error("Valid wrapper expected!");
        }

        if (!m_render_target || !m_render_target->valid()) {
            throw std::runtime_error("Valid render target expected!");
        }

        assert(m_render_target->owned_by_this_thread());
    }

    //------------------------------------------------------------------------------
    // [StereoDrawable]
public:

    //------------------------------------------------------------------------------
    // Returns the OpenGL framebuffer name for the given eye or -1 if submit() was
    // called prior and the drawable is thus no longer considered valid.
    GLuint framebuffer(size_t eye_index) const noexcept override { return ((eye_index < 2) && m_render_target ? m_render_target->framebuffer(eye_index) : GLuint(-1)); }

    //------------------------------------------------------------------------------
    // Returns the viewport (in pixels) for the given eye or zeros if submit() was
    // called prior and the drawable is thus no longer considered valid.
    glm::ivec4 viewport(size_t eye_index) const noexcept override { return ((eye_index < 2) && m_render_target ? m_viewport : glm::ivec4(0)); }

    //------------------------------------------------------------------------------
    // Returns true if both eyes were submitted successfully or false otherwise.
    // The drawable will be invalid after calling submit() in either case. glFlush()
    // is called upon successful submission of both eyes as recommended by the
    // OpenVR spec for use with OpenGL.
    void submit() override
    {
        std::shared_ptr<const RenderTarget> render_target = std::atomic_exchange(&m_render_target, s_invalid_render_target);
        assert(!m_render_target->valid());

        if (!render_target->valid()) {
            return;
        }

        m_wrapper->Render(render_target->color_attachment(0), render_target->color_attachment(1), float(m_audio_timestamp));
    }

    //------------------------------------------------------------------------------
    // {Private}
private:

    static std::shared_ptr<const RenderTarget>      s_invalid_render_target;

    const std::shared_ptr<HWW::HWWrapper>       m_wrapper;
    const double                                m_audio_timestamp;

    std::shared_ptr<const RenderTarget>         m_render_target;
    glm::ivec4                                  m_viewport = {};
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<const WrapperDisplay::RenderTarget> WrapperDisplay::Drawable::s_invalid_render_target(new WrapperDisplay::RenderTarget());

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WrapperDisplay::WrapperDisplay(std::shared_ptr<HWW::HWWrapper> wrapper, size_t width, size_t height)
    : m_wrapper(std::move(wrapper))
    , m_render_target(new RenderTarget(width, height))
{
    if (!m_wrapper) {
        throw std::runtime_error("Valid wrapper expected!");
    }
}

glm::mat4
WrapperDisplay::projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept
{
    if (eye_index == 0) {
        return m_wrapper->GetLeftEyeTransformationMatrix(float(near_z), float(far_z));
    }
    else if (eye_index == 1) {
        return m_wrapper->GetRightEyeTransformationMatrix(float(near_z), float(far_z));
    }
    else {
        glm::mat4(1.0);
    }
}

StereoDrawable_UP
WrapperDisplay::wait_next_drawable() const
{
    if (!m_render_target->acquire()) {
        return nullptr;
    }

    return StereoDrawable_UP(new Drawable(m_wrapper, m_render_target, m_audio_timestamp));
}

StereoDrawable_UP
WrapperDisplay::wait_next_drawable_for(const std::chrono::microseconds& duration, bool* const try_failed) const
{
    const AcquireReleaseWithOwnership::AcquireResult result = m_render_target->try_acquire_for(duration);

    if (try_failed) {
        (*try_failed) = (result == AcquireReleaseWithOwnership::AcquireResult::TRY_FAILED);
    }

    if (!result) {
        return nullptr;
    }

    return StereoDrawable_UP(new Drawable(m_wrapper, m_render_target, m_audio_timestamp));
}

const glm::mat4
WrapperDisplay::hmd_pose() const noexcept
{
    try {
        glm::vec3 hmd_position;
        glm::quat hmd_orientation;

        if (!m_wrapper->GetHMDPose(hmd_position, hmd_orientation)) {
            return glm::mat4(1.0);
        }

        const glm::mat4 hmd_rotation = glm::mat4_cast(glm::normalize(hmd_orientation));
        const glm::mat4 hmd_translation = glm::translate(glm::mat4(1.0), hmd_position);

        return (hmd_translation * hmd_rotation);
    }
    catch (...) {
        return glm::mat4(1.0);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
