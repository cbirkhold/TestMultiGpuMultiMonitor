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

#include "HWWrapper/include/HWWrapper.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "../../Watchdog.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WrapperStereoDisplay::WrapperStereoDisplay(
    std::pair<HDC, HGLRC> context,
    size_t width,
    size_t height,
    ColorSpace color_space,
    std::shared_ptr<HWW::HWWrapper> wrapper
)
    : StereoDisplay(context)
    , m_render_target(width, height, color_space, StereoRenderTarget::CREATE_TAG)
    , m_wrapper(std::move(wrapper))
{
    if (!m_wrapper) {
        throw std::runtime_error("Valid wrapper expected!");
    }

    glGetError();   // Reset OpenGL error.

    m_wrapper->Initialize();
    m_wrapper->SetIPD(DEFAULT_IPD);
    m_wrapper->SetTrackerPredictionTime(0.044f);

    const GLenum error = glGetError();

    if (error != GL_NO_ERROR) {
        m_wrapper_opengl_errors.insert(error);
    }
}

glm::mat4
WrapperStereoDisplay::projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept
{
    if (eye_index == 0) {
        return m_wrapper->GetLeftEyeTransformationMatrix(float(near_z), float(far_z));
    }
    else if (eye_index == 1) {
        return m_wrapper->GetRightEyeTransformationMatrix(float(near_z), float(far_z));
    }
    else {
        return glm::mat4(1.0);
    }
}

void
WrapperStereoDisplay::render(const StereoDisplay& stereo_display, double timestamp) const
{
    const StereoRenderTarget& render_target = stereo_display.render_target();
    stereo_display.make_current();

    assert(wglGetCurrentContext() == opengl_context());
    assert(render_target.single_framebuffer());

    render_target.bind_single_framebuffer();

    glGetError();   // Reset OpenGL error.

    Watchdog::marker("Render", 17);     // ~1.5 frames at 90 FPS

    m_wrapper->SetViewportDimentions(int(render_target.width()), int(render_target.height()));

    m_wrapper->Render(
        m_render_target.color_attachment(size_t(EyeIndex::LEFT)),
        m_render_target.color_attachment(size_t(EyeIndex::RIGHT)),
        float(timestamp)
    );

    bool watchdog_expired = false;

    if (Watchdog::reset_marker() == Watchdog::MARKER_RESULT_PREVIOUS_MARKER_EXPIRED) {
        if (FAIL_IF_WATCHDOG_EXPIRES) {
            watchdog_expired = true;
        }
    }

    const GLenum error = glGetError();

    if (error != GL_NO_ERROR) {
        m_wrapper_opengl_errors.insert(error);
    }

    if (watchdog_expired) {
        std::stringstream stream;
        bool first = true;

        if (watchdog_expired) {
            stream << "Submit marker expired!";
            first = false;
        }

        throw std::runtime_error(stream.str());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WrapperPoseTracker::WrapperPoseTracker(std::shared_ptr<HWW::HWWrapper> wrapper)
    : m_wrapper(std::move(wrapper))
{
    if (!m_wrapper) {
        throw std::runtime_error("Valid wrapper expected!");
    }
}

glm::mat4
WrapperPoseTracker::hmd_pose() const noexcept
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
