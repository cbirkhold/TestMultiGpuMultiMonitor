//
//  OpenVRCompositor.cpp
//  StereoDisplay
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "OpenVRCompositor.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <array>
#include <sstream>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "OpenVRUtils.h"
#include "Utils.h"
#include "Watchdog.h"

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

class OpenVRCompositor::RenderTarget : public AcquireReleaseWithOwnership
{
    //------------------------------------------------------------------------------
     // Construction/Destruction
public:

    RenderTarget()
        : m_width(0)
        , m_height(0)
        , m_color_space(vr::ColorSpace_Linear)
    {
    }

    RenderTarget(size_t width, size_t height, vr::EColorSpace color_space)
        : m_width(width)
        , m_height(height)
        , m_color_space(color_space)
    {
        if ((width == 0) || (height == 0)) {
            throw std::runtime_error("Valid render target size expected!");
        }

        // TODO: Create framebuffer with formats matching the given color space!
        assert((m_color_space == vr::ColorSpace_Linear) && "Not implemented!");

        create_texture_backed_render_targets(m_framebuffers.data(), m_color_attachments.data(), NUM_EYES, m_width, m_height);

        for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
            m_viewports[eye_index] = glm::ivec4(0, 0, m_width, m_height);
            m_bounds[eye_index] = glm::vec4(0.0, 0.0, 1.0, 1.0);
        }
    }

    ~RenderTarget()
    {
        if (valid()) {
            delete_texture_backed_render_targets(m_framebuffers.data(), m_color_attachments.data(), NUM_EYES);
        }
    }

    //------------------------------------------------------------------------------
    // Framebuffers
public:

    bool valid() const { return (m_width > 0 ? true : false); }

    GLuint framebuffer(size_t eye_index) const { return m_framebuffers[eye_index]; }
    GLuint color_attachment(size_t eye_index) const { return m_color_attachments[eye_index]; }

    glm::ivec4 viewport(size_t eye_index) const { return m_viewports[eye_index]; }
    glm::vec4 bounds(size_t eye_index) const { return m_bounds[eye_index]; }

    vr::EColorSpace color_space() const { return m_color_space; }

    //------------------------------------------------------------------------------
    // {Private}
private:

    const size_t                            m_width;                    // Width of the framebuffers
    const size_t                            m_height;                   // Height of the framebuffers

    std::array<GLuint, NUM_EYES>            m_framebuffers = {};        // Framebuffers
    std::array<GLuint, NUM_EYES>            m_color_attachments = {};   // Framebuffer color attachments

    std::array<glm::ivec4, NUM_EYES>        m_viewports = {};           // Viewports into the framebuffers [pixels]
    std::array<glm::vec4, NUM_EYES>         m_bounds = {};              // Viewports into the framebuffers [normalized]

    const vr::EColorSpace                   m_color_space;              // Color space of the framebuffers
};

//------------------------------------------------------------------------------
// OpenVR implementation of StereoDrawable.
//------------------------------------------------------------------------------

class OpenVRCompositor::Drawable : public StereoDrawable
{
    //------------------------------------------------------------------------------
    // Construction/Destruction
public:

    //------------------------------------------------------------------------------
    // Create the drawable and acquire the given render target until submit() is
    // called at which point the render target is released, success or not.
    Drawable(
        vr::IVRCompositor* const compositor,
        std::shared_ptr<const RenderTarget> render_target,
        vr::EVRSubmitFlags submit_flags
    )
        : m_compositor(compositor)
        , m_render_target(std::move(render_target))
        , m_submit_flags(submit_flags)
    {
        if (!m_compositor) {
            throw std::runtime_error("Valid IVRCompositor expected!");
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
    GLuint framebuffer(size_t eye_index) const noexcept override { return (eye_index < NUM_EYES ? m_render_target->framebuffer(eye_index) : GLuint(-1)); }

    //------------------------------------------------------------------------------
    // Returns the viewport (in pixels) for the given eye or zeros if submit() was
    // called prior and the drawable is thus no longer considered valid.
    glm::ivec4 viewport(size_t eye_index) const noexcept override { return (eye_index < NUM_EYES ? m_render_target->viewport(eye_index) : glm::ivec4(0)); }

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
            throw std::runtime_error("Valid render target expected!");
        }

        std::unique_lock<const RenderTarget> lock((*render_target), std::adopt_lock);

        vr::EVRCompositorError errors[NUM_EYES] = {};

        vr::Texture_t vr_texture;
        {
            vr_texture.eType = vr::TextureType_OpenGL;
            vr_texture.eColorSpace = render_target->color_space();
        }

        Watchdog::marker("Submit", 17);     // ~1.5 frames at 90 FPS

        for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
            vr_texture.handle = reinterpret_cast<void*>(static_cast<uintptr_t>(render_target->color_attachment(eye_index)));
            const vr::VRTextureBounds_t bounds = make_texture_bounds(render_target->bounds(eye_index));

            errors[eye_index] = m_compositor->Submit(vr::EVREye(eye_index), &vr_texture, &bounds, m_submit_flags);
        }

        bool watchdog_expired = false;

        if (FAIL_IF_WATCHDOG_EXPIRES && (Watchdog::reset_marker() == Watchdog::MARKER_RESULT_PREVIOUS_MARKER_EXPIRED)) {
            watchdog_expired = true;
        }

        glFlush();

        bool vr_compositor_errors = false;

        for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
            if (errors[eye_index] != vr::VRCompositorError_None) {
                vr_compositor_errors = true;
            }
        }

        if (watchdog_expired || vr_compositor_errors) {
            std::stringstream stream;
            bool first = true;

            if (watchdog_expired) {
                stream << "Submit marker expired!";
                first = false;
            }

            if (vr_compositor_errors) {
                for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
                    if (errors[eye_index] != vr::VRCompositorError_None) {
                        if (!first) { stream << ' '; }
                        stream << "Submit failed: " << (eye_index == 0 ? "left" : "right") << " eye: " << OpenVRUtils::compositor_error_as_english_description(errors[eye_index]);
                        first = false;
                    }
                }
            }

            throw std::runtime_error(stream.str());
        }
    }

    //------------------------------------------------------------------------------
    // {Private}
private:

    static std::shared_ptr<const RenderTarget>      s_invalid_render_target;

    vr::IVRCompositor* const                m_compositor;       // Cached VR compositor instance
    std::shared_ptr<const RenderTarget>     m_render_target;    // Render target shared with the OpenVRCompositor instance
    const vr::EVRSubmitFlags                m_submit_flags;     // Submit flags for the framebuffers
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<const OpenVRCompositor::RenderTarget> OpenVRCompositor::Drawable::s_invalid_render_target(new RenderTarget());

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

OpenVRCompositor::OpenVRCompositor(
    size_t width,
    size_t height,
    vr::EColorSpace color_space,
    vr::EVRSubmitFlags submit_flags)
    : m_compositor(vr::VRCompositor())
    , m_system(vr::VRSystem())
    , m_render_target(new RenderTarget(width, height, color_space))
    , m_submit_flags(submit_flags)
{
    if (!m_compositor && !m_system) {
        throw std::runtime_error("Valid OpenVR compositor/system expected!");
    }

    for (size_t i = 0; i < m_render_poses.size(); ++i) {
        m_render_poses[i].eTrackingResult = vr::TrackingResult_Uninitialized;
        m_render_poses[i].bPoseIsValid = false;
        m_render_poses[i].bDeviceIsConnected = false;
    }
}

void
OpenVRCompositor::wait_get_poses()
{
    Watchdog::marker("WaitGetPoses", 100);

    const vr::EVRCompositorError error = m_compositor->WaitGetPoses(
        m_render_poses.data(),
        uint32_t(m_render_poses.size()),
        nullptr,
        0);

    bool watchdog_expired = false;

    if (FAIL_IF_WATCHDOG_EXPIRES && (Watchdog::reset_marker() == Watchdog::MARKER_RESULT_PREVIOUS_MARKER_EXPIRED)) {
        watchdog_expired = true;
    }

    bool vr_compositor_errors = (error != vr::VRCompositorError_None);

    if (watchdog_expired || vr_compositor_errors) {
        std::stringstream stream;
        bool first = true;

        if (watchdog_expired) {
            stream << "WaitGetPoses marker expired!";
            first = false;
        }

        if (vr_compositor_errors) {
            if (!first) { stream << ' '; }
            stream << "WaitGetPoses failed: " << OpenVRUtils::compositor_error_as_english_description(error);
            first = false;
        }

        throw std::runtime_error(stream.str());
    }
}

glm::mat4
OpenVRCompositor::projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept
{
    if (eye_index >= 2) {
        return glm::mat4(1.0);
    }

    glm::mat4 m = OpenVRUtils::glm_from_hmd_matrix(m_system->GetProjectionMatrix(vr::EVREye(eye_index), float(near_z), float(far_z)));
    m *= glm::inverse(OpenVRUtils::glm_from_hmd_matrix(m_system->GetEyeToHeadTransform(vr::EVREye(eye_index))));
    return m;
}

StereoDrawable_UP
OpenVRCompositor::wait_next_drawable() const
{
    if (!m_render_target->acquire()) {
        return nullptr;
    }

    return StereoDrawable_UP(new Drawable(m_compositor, m_render_target, m_submit_flags));
}

StereoDrawable_UP
OpenVRCompositor::wait_next_drawable_for(const std::chrono::microseconds& duration, bool* const try_failed) const
{
    const AcquireReleaseWithOwnership::AcquireResult result = m_render_target->try_acquire_for(duration);

    if (try_failed) {
        (*try_failed) = (result == AcquireReleaseWithOwnership::AcquireResult::TRY_FAILED);
    }

    if (!result) {
        return nullptr;
    }

    return StereoDrawable_UP(new Drawable(m_compositor, m_render_target, m_submit_flags));
}

glm::mat4
OpenVRCompositor::hmd_pose() const noexcept
{
    return OpenVRUtils::glm_from_hmd_matrix(m_render_poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
