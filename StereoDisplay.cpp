//
//  StereoDisplay.h
//  StereoDisplay
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "StereoDisplay.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <sstream>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "OpenVRUtils.h"
#include "Watchdog.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

    vr::VRTextureBounds_t make_texture_bounds(glm::vec4 bounds_)
    {
        vr::VRTextureBounds_t bounds = {};

        bounds.uMin = bounds_.x;
        bounds.vMin = bounds_.y;
        bounds.uMax = bounds_.z;
        bounds.vMax = bounds_.w;

        return bounds;
    }

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StereoRenderTarget::StereoRenderTarget(size_t width, size_t height, ColorSpace color_space, const default_framebuffer_tag&)
    : m_width(width)
    , m_height(height)
    , m_color_space(color_space)
{
    validate_initializer_list();
    init_single_framebuffer();
}

StereoRenderTarget::StereoRenderTarget(size_t width, size_t height, ColorSpace color_space, const create_tag&)
    : m_width(width)
    , m_height(height)
    , m_color_space(color_space)
{
    validate_initializer_list();

    toolbox::OpenGLFramebuffer::create_texture_backed(
        m_framebuffers.data(),
        m_color_attachments.data(),
        nullptr,
        NUM_EYES,
        (m_width / 2),
        m_height
    );

    for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
        m_viewports[eye_index] = glm::ivec4(0, 0, (m_width / 2), m_height);
        m_bounds[eye_index] = glm::vec4(0.0, 0.0, 1.0, 1.0);
    }
}

StereoRenderTarget::StereoRenderTarget(size_t width, size_t height, ColorSpace color_space, const create_single_framebuffer_tag&)
    : m_width(width)
    , m_height(height)
    , m_color_space(color_space)
{
    validate_initializer_list();

    toolbox::OpenGLFramebuffer::create_texture_backed(
        m_framebuffers.data(),
        m_color_attachments.data(),
        nullptr,
        1,
        m_width,
        m_height
    );

    m_framebuffers[1] = m_framebuffers[0];
    m_color_attachments[1] = m_color_attachments[0];

    init_single_framebuffer();
}

StereoRenderTarget::~StereoRenderTarget()
{
    toolbox::OpenGLFramebuffer::delete_texture_backed(
        m_framebuffers.data(),
        m_color_attachments.data(),
        nullptr,
        (single_framebuffer() ? 1 : NUM_EYES)
    );
}

void
StereoRenderTarget::bind_single_framebuffer() const
{
    assert(single_framebuffer());

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer(0));
    glViewport(0, 0, GLsizei(m_width), GLsizei(m_height));
    glDisable(GL_SCISSOR_TEST);
}

void
StereoRenderTarget::bind_eye(size_t eye_index) const
{
    const glm::ivec4 viewport = this->viewport(eye_index);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer(eye_index));
    glViewport(viewport.x, viewport.y, viewport.z, viewport.w);

    if (single_framebuffer()) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(viewport.x, viewport.y, viewport.z, viewport.w);
    }
}

void
StereoRenderTarget::unbind_eye() const
{
    if (single_framebuffer()) {
        glDisable(GL_SCISSOR_TEST);
    }
}

void
StereoRenderTarget::validate_initializer_list()
{
    if ((m_width == 0) || (m_height == 0)) {
        throw std::runtime_error("Valid render target size expected!");
    }
}

void
StereoRenderTarget::init_single_framebuffer() noexcept
{
    m_viewports[0] = glm::ivec4(0, 0, (m_width / 2), m_height);
    m_viewports[1] = glm::ivec4((m_width / 2), 0, (m_width / 2), m_height);

    m_bounds[0] = glm::vec4(0.0, 0.0, 0.5, 1.0);
    m_bounds[1] = glm::vec4(0.5, 0.0, 1.0, 1.0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StereoDisplay::StereoDisplay(std::pair<HDC, HGLRC> context)
    : m_display_context(context.first)
    , m_opengl_context(context.second)
{
    if (!m_display_context) {
        throw std::runtime_error("Valid display context expected!");
    }

    if (!m_opengl_context) {
        throw std::runtime_error("Valid OpenGL context expected!");
    }

    make_current();
}

void StereoDisplay::make_current() const
{
    if (!wglMakeCurrent(m_display_context, m_opengl_context)) {
        throw std::runtime_error("Failed to make context current!");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WindowStereoDisplay::WindowStereoDisplay(
    std::pair<HDC, HGLRC> context,
    size_t width,
    size_t height,
    ColorSpace color_space,
    double fov,
    double ipd
)
    : StereoDisplay(context)
    , m_render_target(width, height, color_space, StereoRenderTarget::DEFAULT_FRAMEBUFFER_TAG)
    , m_fov(fov)
    , m_ipd(ipd)
{
    if (m_fov <= 0.0) {
        throw std::runtime_error("Valid focal length expected!");
    }
}

glm::mat4
WindowStereoDisplay::projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept
{
    if (eye_index >= 2) {
        return glm::mat4(1.0);
    }

    const glm::mat4 p = glm::perspectiveFov(
        glm::mat4::value_type(m_fov),
        glm::mat4::value_type(m_render_target.width()),
        glm::mat4::value_type(m_render_target.height()),
        glm::mat4::value_type(near_z),
        glm::mat4::value_type(far_z)
    );

    const glm::mat4 t = glm::translate(glm::vec3(((m_ipd / 2.0) * (eye_index == size_t(EyeIndex::LEFT) ? -1.0 : 1.0)), 0.0, 0.0));

    return (p * glm::inverse(t));
}

void WindowStereoDisplay::submit() const
{
    if (!SwapBuffers(display_context())) {
        throw std::runtime_error("Failed to swap buffers!");
    }
}

void WindowStereoDisplay::render(const StereoDisplay& stereo_display, double timestamp) const
{
    const StereoRenderTarget& render_target = stereo_display.render_target();
    stereo_display.make_current();

    for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
        const glm::ivec4 src_viewport = m_render_target.viewport(eye_index);
        const glm::ivec4 dst_viewport = render_target.viewport(eye_index);

        glBlitNamedFramebuffer(
            m_render_target.framebuffer(eye_index), render_target.framebuffer(eye_index),
            src_viewport.x, src_viewport.y, (src_viewport.x + src_viewport.z), (src_viewport.y + src_viewport.w),
            dst_viewport.x, dst_viewport.y, (dst_viewport.x + dst_viewport.z), (dst_viewport.y + dst_viewport.w),
            GL_COLOR_BUFFER_BIT, GL_LINEAR
        );
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

OpenVRStereoDisplay::OpenVRStereoDisplay(
    std::pair<HDC, HGLRC> context,
    vr::EVRSubmitFlags submit_flags,
    size_t width,
    size_t height,
    ColorSpace color_space,
    const create_single_framebuffer_tag&
)
    : StereoDisplay(context)
    , m_render_target(width, height, color_space, StereoRenderTarget::CREATE_SINGLE_FRAMEBUFFER_TAG)
    , m_system(vr::VRSystem())
    , m_compositor(vr::VRCompositor())
    , m_submit_flags(submit_flags)
{
    validate_initializer_list();
    cache_submit_parameters();
}

OpenVRStereoDisplay::OpenVRStereoDisplay(
    std::pair<HDC, HGLRC> context,
    vr::EVRSubmitFlags submit_flags,
    size_t width,
    size_t height,
    ColorSpace color_space
)
    : StereoDisplay(context)
    , m_render_target(width, height, color_space, StereoRenderTarget::CREATE_TAG)
    , m_system(vr::VRSystem())
    , m_compositor(vr::VRCompositor())
    , m_submit_flags(submit_flags)
{
    validate_initializer_list();
    cache_submit_parameters();
}

glm::mat4
OpenVRStereoDisplay::projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept
{
    if (eye_index >= 2) {
        return glm::mat4(1.0);
    }

    glm::mat4 m = OpenVRUtils::glm_from_hmd_matrix(m_system->GetProjectionMatrix(vr::EVREye(eye_index), float(near_z), float(far_z)));
    m *= glm::inverse(OpenVRUtils::glm_from_hmd_matrix(m_system->GetEyeToHeadTransform(vr::EVREye(eye_index))));
    return m;
}

void
OpenVRStereoDisplay::submit() const
{
    assert(wglGetCurrentDC() == display_context());
    assert(wglGetCurrentContext() == opengl_context());

    vr::EVRCompositorError errors[NUM_EYES] = {};

    Watchdog::marker("Submit", 17);     // ~1.5 frames at 90 FPS

    for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
        errors[eye_index] = m_compositor->Submit(
            vr::EVREye(eye_index),
            &m_submit_textures[eye_index],
            &m_submit_bounds[eye_index],
            m_submit_flags
        );
    }

    bool watchdog_expired = false;

    if (Watchdog::reset_marker() == Watchdog::MARKER_RESULT_PREVIOUS_MARKER_EXPIRED) {
        if (FAIL_IF_WATCHDOG_EXPIRES) {
            watchdog_expired = true;
        }
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

void
OpenVRStereoDisplay::render(const StereoDisplay& stereo_display, double timestamp) const
{
    throw std::runtime_error("Rendering to another target is not supported by this implementation!");
}

void
OpenVRStereoDisplay::validate_initializer_list()
{
    if (!m_compositor && !m_system) {
        throw std::runtime_error("Valid OpenVR compositor/system expected!");
    }

    if ((m_submit_flags != vr::Submit_Default) && (m_submit_flags != vr::Submit_LensDistortionAlreadyApplied)) {
        throw std::runtime_error("Valid submit flags expected!");
    }
}

void
OpenVRStereoDisplay::cache_submit_parameters()
{
    for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
        m_submit_textures[eye_index].handle = reinterpret_cast<void*>(static_cast<uintptr_t>(m_render_target.color_attachment(eye_index)));
        m_submit_textures[eye_index].eType = vr::TextureType_OpenGL;

        switch (m_render_target.color_space()) {
        case ColorSpace::LINEAR: m_submit_textures[eye_index].eColorSpace = vr::ColorSpace_Linear; break;
        case ColorSpace::SRGB: m_submit_textures[eye_index].eColorSpace = vr::ColorSpace_Gamma; break;
        }

        m_submit_bounds[eye_index] = make_texture_bounds(m_render_target.bounds(eye_index));
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
