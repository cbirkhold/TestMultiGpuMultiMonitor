
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// STL
//------------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <deque>
#include <future>
#include <iostream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <thread>

//------------------------------------------------------------------------------
// Platform
//------------------------------------------------------------------------------

#include "__WindowsApi.h"

//------------------------------------------------------------------------------
// GLEW (OpenGL), GLFW, GLM, NVAPI, OpenVR
//------------------------------------------------------------------------------

#include "_GLEWApi.h"
#include "_GLFWApi.h"
#include "_GLMApi.h"
#include "_NVApi.h"
#include "_OpenVRApi.h"

//------------------------------------------------------------------------------
// Wrapper
//------------------------------------------------------------------------------

#include <Wrapper.h>
#include <HWWrapper.h>

//------------------------------------------------------------------------------
// Toolbox
//------------------------------------------------------------------------------

#include "CppUtils.h"
#include "DisplayConfiguration.h"
#include "OpenGLUtils.h"
#include "OpenVRUtils.h"
#include "RenderPoints.h"
#include "Watchdog.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    //------------------------------------------------------------------------------
    // Constants
    //------------------------------------------------------------------------------

#ifdef NDEBUG
    constexpr bool IS_DEBUG_BUILD = false;
#else // NDEBUG
    constexpr bool IS_DEBUG_BUILD = true;
#endif // ..., NDEBUG

    constexpr size_t NUM_EYES = 2;
    constexpr size_t EYE_INDEX_LEFT = 0;
    constexpr size_t EYE_INDEX_RIGHT = 1;

    constexpr int GL_CONTEXT_VERSION_MAJOR = 4;
    constexpr int GL_CONTEXT_VERSION_MINOR = 6;

    constexpr int GL_OPENGL_DEBUG_CONTEXT = (IS_DEBUG_BUILD ? GLFW_TRUE : GLFW_FALSE);

    constexpr char GLSL_VERSION[] = "#version 460";

    //------------------------------------------------------------------------------
    // Configuration
    //------------------------------------------------------------------------------

    constexpr bool DEBUG_WRAPPER_VS_OPENVR_HMD_POSE = false;

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    //------------------------------------------------------------------------------
    // Utilities
    //------------------------------------------------------------------------------

#if GL_VERSION_4_3
    void APIENTRY gl_message_callback(GLenum source_, GLenum type_, GLuint id, GLenum severity, GLsizei length, const GLchar* const message, const void* const userParam)
    {
        const char* source = "<unknown source>";

        switch (source_) {
        case GL_DEBUG_SOURCE_API:             source = "api";             break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   source = "window system";   break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: source = "shader compiler"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     source = "third party";     break;
        case GL_DEBUG_SOURCE_APPLICATION:     source = "application";     break;
        case GL_DEBUG_SOURCE_OTHER:           source = "other";           break;
        default: assert(false && "Unknown GL debug message source!");     break;
        }

        const char* type = "<unknown type>";

        switch (type_) {
        case GL_DEBUG_TYPE_ERROR:               type = "error";               break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type = "deprecated behavior"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  type = "undefined behavior";  break;
        case GL_DEBUG_TYPE_PORTABILITY:         type = "portability";         break;
        case GL_DEBUG_TYPE_PERFORMANCE:         type = "performance";         break;
        case GL_DEBUG_TYPE_OTHER:               type = "other";               break;
        case GL_DEBUG_TYPE_MARKER:              type = "marker";              break;
        case GL_DEBUG_TYPE_PUSH_GROUP:          type = "push group";          break;
        case GL_DEBUG_TYPE_POP_GROUP:           type = "pop group";           break;
        default: assert(false && "Unknown GL debug message type!");           break;
        }

        const char* log_level = "<unknown level>";

        switch (severity) {
        case GL_DEBUG_SEVERITY_NOTIFICATION: log_level = "Info";        break;
        case GL_DEBUG_SEVERITY_HIGH:         log_level = "Error";       break;
        case GL_DEBUG_SEVERITY_MEDIUM:       log_level = "Warning";     break;
        case GL_DEBUG_SEVERITY_LOW:          log_level = "Info";        break;
        default: assert(false && "Unknown GL debug message severity!"); break;
        }

        std::cout << log_level << ": OpenGL debug: " << source <<": " << type << ": " << message << std::endl;
    }
#endif // GL_VERSION_4_3

    void gl_init_debug_messages()
    {
#if GL_VERSION_4_3
#ifndef NDEBUG
        glDebugMessageCallback(&gl_message_callback, &gl_message_callback);

        if ((1)) {
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_MEDIUM, 0, nullptr, GL_TRUE);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_HIGH, 0, nullptr, GL_TRUE);
        }
        else if ((1)) {
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        }
        else {
            glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageControl(GL_DEBUG_SOURCE_WINDOW_SYSTEM, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageControl(GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageControl(GL_DEBUG_SOURCE_THIRD_PARTY, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageControl(GL_DEBUG_SOURCE_APPLICATION, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            glDebugMessageControl(GL_DEBUG_SOURCE_OTHER, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        }
#endif // NDEBUG
#endif // GL_VERSION_4_3
    }

     ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    HWND stereo_display_window = nullptr;

    LRESULT CALLBACK mosaic_window_callback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        const bool window_created = (GetPropA(hWnd, "MOSAIC_WINDOW_CREATED") != nullptr);

        if ((uMsg == WM_ERASEBKGND) || (uMsg == WM_PAINT)) {
            if (!window_created) {
                return DefWindowProc(hWnd, uMsg, wParam, lParam);
            }
            else {
                //------------------------------------------------------------------------------
                // "An application returns zero if it processes this message." [WM_PAINT docs]
                return 0;
            }
        }

        if ((uMsg == WM_NCCREATE) || (uMsg == WM_CREATE)) {
            const CREATESTRUCT* const create_struct = reinterpret_cast<const CREATESTRUCT*>(lParam);
            std::cout << "Window created: (" << create_struct->x << " / " << create_struct->y << ") [" << create_struct->cx << " x " << create_struct->cy << "]" << std::endl;
        }
        else if (window_created && (hWnd != stereo_display_window)) {
            std::cerr << "Warning: Received message not associated with the stereo display (msg=" << toolbox::StlUtils::hex_insert(uMsg) << ", param=" << wParam << ", param" << toolbox::StlUtils::hex_insert(lParam) << ")!" << std::endl;
        }

        if (uMsg == WM_DISPLAYCHANGE) {
            std::cerr << "Warning: Display change occurred. This application is not designed to handle such changes at runtime (msg=" << toolbox::StlUtils::hex_insert(uMsg) << ", param=" << wParam << ", param=" << toolbox::StlUtils::hex_insert(lParam) << ")!" << std::endl;
        }

        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    int control_window_x = 0;
    int control_window_y = 0;
    int control_window_width = 0;
    int control_window_height = 0;

    void control_window_key_callback(GLFWwindow* const window, int key, int scancode, int action, int mods)
    {
        if (action == GLFW_PRESS) {
            switch (key) {
            case GLFW_KEY_ESCAPE:
                break;

            case GLFW_KEY_SPACE:
                break;

            default:
                break;
            }
        }
    }

    GLFWwindow* create_control_window(std::shared_ptr<Display> control_display)
    {
        //------------------------------------------------------------------------------
        // Set shared GLFW window hints.
        glfwDefaultWindowHints();

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, GL_CONTEXT_VERSION_MAJOR);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, GL_CONTEXT_VERSION_MINOR);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_OPENGL_DEBUG_CONTEXT);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // So we can move it in place BEFORE showing it.

        //------------------------------------------------------------------------------
        // Create main GLFW window.
        const long inset = 95;

        control_window_x = (control_display->virtual_screen_rect().m_x + inset);
        control_window_y = (control_display->virtual_screen_rect().m_y + inset);
        control_window_width = ((control_display->virtual_screen_rect().m_width / 2) - inset);
        control_window_height = ((control_display->virtual_screen_rect().m_height / 2) - inset);

        std::cout << "Control window: (" << control_window_x << " / " << control_window_y << ") [" << control_window_width << " x " << control_window_height << "]" << std::endl;

        GLFWwindow* const control_window = glfwCreateWindow(control_window_width, control_window_height, "VMI Player", NULL, NULL);

        if (!control_window) {
            throw std::runtime_error("Failed to create main window!");
        }

        glfwSetKeyCallback(control_window, &control_window_key_callback);
        glfwSetWindowPos(control_window, control_window_x, control_window_y);
        glfwShowWindow(control_window);

        //------------------------------------------------------------------------------
        // ...
        return control_window;
    }

    //------------------------------------------------------------------------------
    // Create a native window (not GLFW) for the Mosaic/OpenVR window. We don't need
    // event handling and prefer full direct control.
    //------------------------------------------------------------------------------

    HWND create_stereo_display_window(std::shared_ptr<Display> display, const PIXELFORMATDESCRIPTOR& pixel_format_desc)
    {
        //------------------------------------------------------------------------------
        // Register a window class.
        WNDCLASSA wc = {};
        {
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = mosaic_window_callback;
            wc.cbClsExtra = 0;
            wc.cbWndExtra = 0;
            wc.hInstance = NULL;
            wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = NULL;
            wc.lpszMenuName = NULL;
            wc.lpszClassName = "VMI Player Mosaic Window";
        }

        RegisterClassA(&wc);

        //------------------------------------------------------------------------------
        // "An OpenGL window should be created with the WS_CLIPCHILDREN and
        // WS_CLIPSIBLINGS styles. Additionally, the window class attribute should NOT
        // include the CS_PARENTDC style." [SetPixelFormat documentation]
        const DWORD style = (WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

        //------------------------------------------------------------------------------
        // Create a 'full screen' window.
        RECT window_rect = {};
        {
            SetRect(&window_rect,
                display->virtual_screen_rect().m_x,
                display->virtual_screen_rect().m_y,
                (display->virtual_screen_rect().m_x + display->virtual_screen_rect().m_width),
                (display->virtual_screen_rect().m_y + display->virtual_screen_rect().m_height));

            AdjustWindowRect(&window_rect, style, FALSE);
        }

        const HWND window = CreateWindowA(wc.lpszClassName, "Mosaic Window",
            style, window_rect.left, window_rect.top, (window_rect.right - window_rect.left), (window_rect.bottom - window_rect.top),
            nullptr, nullptr, nullptr, nullptr);

        if (window == NULL) {
            throw std::runtime_error("Failed to create window!");
        }

        ShowWindow(window, SW_SHOWDEFAULT);
        UpdateWindow(window);
        SetPropA(window, "MOSAIC_WINDOW_CREATED", HANDLE(1));

        //------------------------------------------------------------------------------
        // Setup the display context.
        const HDC display_context = GetDC(window);

        const int pixel_format = ChoosePixelFormat(display_context, &pixel_format_desc);

        if (pixel_format == 0) {
            throw std::runtime_error("Failed to choose pixel format!");
        }

        if (SetPixelFormat(display_context, pixel_format, &pixel_format_desc) != TRUE) {
            throw std::runtime_error(" Failed to set pixel format!");
        }

        ReleaseDC(window, display_context);

        //------------------------------------------------------------------------------
        // ...
        return window;
    }

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    enum class PoseTrackerMode {
        UI,
        OPENVR,
        WRAPPER,
    };

    PoseTrackerMode pose_tracker_mode = PoseTrackerMode::UI;

    class UIPoseTracker : public PoseTracker
    {
    public:

        void wait_get_poses() {}
        glm::mat4 hmd_pose() const noexcept { return glm::mat4(1.0); }
    };

    class OpenVRPoseTracker : public PoseTracker
    {
        //------------------------------------------------------------------------------
        // Configuration/Types
    public:

        static constexpr bool FAIL_IF_WATCHDOG_EXPIRES = false;

        //------------------------------------------------------------------------------
        // A list of tracked device poses large enough to hold the maximum.
        typedef std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount> TrackedDevicePoses;

        //------------------------------------------------------------------------------
        // Construction/Destruction
    public:

        OpenVRPoseTracker()
            : m_compositor(vr::VRCompositor())
        {
        }

        //------------------------------------------------------------------------------
        // [PoseTracker]
    public:

        void wait_get_poses() override
        {
            Watchdog::marker("WaitGetPoses", 100);

            const vr::EVRCompositorError error = m_compositor->WaitGetPoses(
                m_render_poses.data(),
                uint32_t(m_render_poses.size()),
                nullptr,
                0);

            bool watchdog_expired = false;

            if (Watchdog::reset_marker() == Watchdog::MARKER_RESULT_PREVIOUS_MARKER_EXPIRED) {
                if (FAIL_IF_WATCHDOG_EXPIRES) {
                    watchdog_expired = true;
                }
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

        glm::mat4 hmd_pose() const noexcept override
        {
            return OpenVRUtils::glm_from_hmd_matrix(m_render_poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
        }

    private:

        vr::IVRCompositor* const        m_compositor;       // Cached VR compositor instance
        TrackedDevicePoses              m_render_poses = {};
    };

    std::unique_ptr<PoseTracker> pose_tracker_ui;
    std::unique_ptr<PoseTracker> pose_tracker_openvr;
    std::unique_ptr<PoseTracker> pose_tracker_wrapper;

    enum class DisplayMode {
        //------------------------------------------------------------------------------
        // Render to window default framebuffer, then swap buffers. This mode is chosen
        // when neither a Mosaic nor OpenVR display are available (this display
        // configuration is only valid in a debug build) or if a Mosaic display is
        // available but the wrapper is not enabled.
        WINDOW_UNDISTORTED,

        //------------------------------------------------------------------------------
        // Render to OpenVR stereo drawable (user framebuffer), then submit to OpenVR
        // compositor (in direct or extended mode). This mode is chosen if the OpenVR
        // display is selected.
        OPENVR_COMPOSITOR_DISTORTED,

        //------------------------------------------------------------------------------
        // Render to wrapper stereo drawable (user framebuffer), let wrapper distort
        // into window default framebuffer, then swap buffers. This mode is chosen when
        // the Mosaic display is selected, for rendering to the Target Hardware or
        // testing of that code path with a regular display.
        WRAPPER_TO_WINDOW_DISTORTED,

        //------------------------------------------------------------------------------
        // Render to wrapper stereo drawable (user framebuffer), let wrapper distort
        // into user framebuffer, then submit to OpenVR compositor as distortion already
        // applied. This mode is for development purposes only, for testing the Target
        // Hardware code path while rendering to an OpenVR HMD.
        WRAPPER_TO_OPENVR_COMPOSITOR_DISTORTED,
    };

    DisplayMode display_mode = DisplayMode::WINDOW_UNDISTORTED;

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    constexpr size_t PER_GPU_PASS_FRAMEBUFFER_WIDTH = 2048;
    constexpr size_t PER_GPU_PASS_FRAMEBUFFER_HEIGHT = 2048;

    constexpr size_t RENDER_POINTS_GRID_SIZE = 64;

    std::pair<HDC, HGLRC> primary_context;
    std::pair<HDC, HGLRC> support_context;

    std::shared_ptr<HWW::HWWrapper> wrapper;
    std::set<GLenum> wrapper_opengl_errors;

    std::shared_ptr<Display> stereo_display;

    std::unique_ptr<StereoDisplay> window_display;
    std::unique_ptr<StereoDisplay> openvr_display;
    std::unique_ptr<StereoDisplay> wrapper_display;

    std::future<void> render_thread;
    std::atomic_bool exit_render_thread = false;
    std::atomic_size_t render_thread_frame_index = 0;

    GLuint support_framebuffer = 0;
    GLuint support_color_attachment = 0;

    GLuint support_framebuffer_copy = 0;
    GLuint support_color_attachment_copy = 0;

    constexpr size_t CONTEXT_INDEX_PRIMARY = 0;
    constexpr size_t CONTEXT_INDEX_SUPPORT = 1;

    GLuint render_points_programs[2] = {};
    GLuint render_points_vao[2] = {};

    float rect[4] = { -1.0, -1.0, 2.0, 2.0 };

    float mvp[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };

    void render_points(GLuint& vao, size_t num_draws, const glm::mat4& hmd_pose, const glm::mat4& projection_matrix, const float* const color_mask)
    {
        glm::mat4 pose(1.0);
        pose[3] = hmd_pose[3];
        pose[3].z -= 1.0;

        const glm::mat4 mvp = ((projection_matrix * glm::inverse(hmd_pose)) * pose);
        RenderPoints::set_mvp(reinterpret_cast<const GLfloat*>(&mvp));
        RenderPoints::set_color_mask(color_mask);

        for (size_t i = 0; i < num_draws; ++i) {
            RenderPoints::draw(vao, RENDER_POINTS_GRID_SIZE);
        }
    }

    void initialize_render_thread()
    {
        //------------------------------------------------------------------------------
        // Support context objects.
        //------------------------------------------------------------------------------

        if (!wglMakeCurrent(support_context.first, support_context.second)) {
            throw std::runtime_error("Failed to make OpenGL context current!");
        }

        gl_init_debug_messages();

        toolbox::OpenGLFramebuffer::create_texture_backed(&support_framebuffer, &support_color_attachment, nullptr, 1, PER_GPU_PASS_FRAMEBUFFER_WIDTH, PER_GPU_PASS_FRAMEBUFFER_HEIGHT);
        render_points_programs[CONTEXT_INDEX_SUPPORT] = RenderPoints::create_program();

        //------------------------------------------------------------------------------
        // Primary context objects.
        //------------------------------------------------------------------------------

        if (!wglMakeCurrent(primary_context.first, primary_context.second)) {
            throw std::runtime_error("Failed to make OpenGL context current!");
        }

        gl_init_debug_messages();

        toolbox::OpenGLFramebuffer::create_texture_backed(&support_framebuffer_copy, &support_color_attachment_copy, nullptr, 1, PER_GPU_PASS_FRAMEBUFFER_WIDTH, PER_GPU_PASS_FRAMEBUFFER_HEIGHT);
        render_points_programs[CONTEXT_INDEX_PRIMARY] = RenderPoints::create_program();
    }

    void finalize_render_thread() noexcept
    {
        //------------------------------------------------------------------------------
        // Support context objects.
        //------------------------------------------------------------------------------

        if (wglMakeCurrent(support_context.first, support_context.second)) {
            toolbox::OpenGLFramebuffer::delete_texture_backed(&support_framebuffer, &support_color_attachment, nullptr, 1);
            glDeleteProgram(render_points_programs[CONTEXT_INDEX_SUPPORT]);
        }

        //------------------------------------------------------------------------------
        // Primary context objects.
        //------------------------------------------------------------------------------

        if (wglMakeCurrent(primary_context.first, primary_context.second)) {
            glDeleteProgram(render_points_programs[CONTEXT_INDEX_PRIMARY]);
        }
    }

    void render(const StereoDisplay& stereo_display, const glm::mat4& hmd_pose, const glm::mat4& wrapper_pose, const std::array<glm::mat4, NUM_EYES>& projection_matrices, double fraction)
    {
        //------------------------------------------------------------------------------
        // Support context.
        if (!wglMakeCurrent(support_context.first, support_context.second)) {
            throw std::runtime_error("Failed to make OpenGL context current!");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, support_framebuffer);
        glViewport(0, 0, PER_GPU_PASS_FRAMEBUFFER_WIDTH, PER_GPU_PASS_FRAMEBUFFER_HEIGHT);
        glDisable(GL_SCISSOR_TEST);

        if ((1)) {
            glClearColor(0.25, 0.25, 0.25, 1.0);
            glClear(GL_COLOR_BUFFER_BIT);

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_MAX);

            glUseProgram(render_points_programs[CONTEXT_INDEX_SUPPORT]);
            RenderPoints::set_rect(rect);

            const float color_masks[2][4] = { { 1.0f, 0.0f, 0.0, 1.0f }, { 0.0f, 1.0f, 0.0, 1.0f } };
            render_points(render_points_vao[CONTEXT_INDEX_SUPPORT], 40, hmd_pose, projection_matrices[EYE_INDEX_LEFT], color_masks[0]);
            render_points(render_points_vao[CONTEXT_INDEX_SUPPORT], 40, wrapper_pose, projection_matrices[EYE_INDEX_LEFT], color_masks[1]);

            glDisable(GL_BLEND);
        }
        else {
            glClearColor(0.25, 0.5, GLclampf(fraction), 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        //------------------------------------------------------------------------------
        // Copy result of support context to primary context.
        if ((0)) {
            constexpr size_t NUM_TILES = 8;

            for (size_t v = 0; v < 1/*NUM_TILES*/; ++v) {
                const GLint y = GLint((PER_GPU_PASS_FRAMEBUFFER_HEIGHT / NUM_TILES) * v);

                for (size_t u = 0; u < 1/*NUM_TILES*/; ++u) {
                    constexpr GLint LEVEL = 0;
                    constexpr GLint Z = 0;
                    constexpr GLint DEPTH = 1;

                    const GLint x = GLint((PER_GPU_PASS_FRAMEBUFFER_WIDTH / NUM_TILES) * u);

                    wglCopyImageSubDataNV(
                        support_context.second, support_color_attachment,
                        GL_TEXTURE_2D, LEVEL, x, y, Z,
                        primary_context.second, support_color_attachment_copy,
                        GL_TEXTURE_2D, LEVEL, x, y, Z,
                        (PER_GPU_PASS_FRAMEBUFFER_WIDTH / NUM_TILES / 2), (PER_GPU_PASS_FRAMEBUFFER_HEIGHT / NUM_TILES / 2), DEPTH);
                }
            }
        }
        else if ((0)) {
            constexpr GLint LEVEL = 0;
            constexpr GLint X = 0;
            constexpr GLint Y = 0;
            constexpr GLint Z = 0;
            constexpr GLint DEPTH = 1;

            wglCopyImageSubDataNV(
                support_context.second, support_color_attachment,
                GL_TEXTURE_2D, LEVEL, X, Y, Z,
                primary_context.second, support_color_attachment_copy,
                GL_TEXTURE_2D, LEVEL, X, Y, Z,
                PER_GPU_PASS_FRAMEBUFFER_WIDTH, PER_GPU_PASS_FRAMEBUFFER_HEIGHT, DEPTH);
        }

        const GLsync support_context_complete = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();

        //------------------------------------------------------------------------------
        // Primary context.
        const StereoRenderTarget& render_target = stereo_display.render_target();
        stereo_display.make_current();

        for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
            render_target.bind_eye(eye_index);

            if ((1)) {
                glClearColor(0.25, 0.25, 0.25, 1.0);
                glClear(GL_COLOR_BUFFER_BIT);

                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glBlendEquation(GL_MAX);

                glUseProgram(render_points_programs[CONTEXT_INDEX_PRIMARY]);
                RenderPoints::set_rect(rect);

                const float color_masks[2][4] = { { 1.0f, 0.0f, 0.0, 1.0f }, { 0.0f, 1.0f, 0.0, 1.0f } };
                render_points(render_points_vao[CONTEXT_INDEX_PRIMARY], 20, hmd_pose, projection_matrices[eye_index], color_masks[0]);
                render_points(render_points_vao[CONTEXT_INDEX_PRIMARY], 20, wrapper_pose, projection_matrices[eye_index], color_masks[1]);

                glDisable(GL_BLEND);
            }
            else {
                glClearColor(0.5, 0.25, GLclampf(fraction), 1.0);
                glClear(GL_COLOR_BUFFER_BIT);
            }
        }

        render_target.unbind_eye();

        //------------------------------------------------------------------------------
        // Wait for support context to complete rendering for this frame.
        glWaitSync(support_context_complete, 0, GL_TIMEOUT_IGNORED);
    }

    void render_loop()
    {
        //------------------------------------------------------------------------------
        // Evaluate initial eye projection matrices. This can either come from the
        // wrapper or directly from OpenVR.
        constexpr double near_z = 0.1;
        constexpr double far_z = 32.0;

        //------------------------------------------------------------------------------
        // Render loop.
        double time = 0.0;

        for (size_t frame_index = 0; !exit_render_thread; ++frame_index) {
            double seconds = 0.0;
            const double fraction = modf(time, &seconds);

            //------------------------------------------------------------------------------
            // If the OpenVR compositor is used at all we must call WaitGetPoses() to keep
            // the app 'active' from the perspective of OpenVR. This is independently of
            // whether the OpenVR pose is actually used.
            if ((pose_tracker_mode == PoseTrackerMode::OPENVR) ||
                (display_mode == DisplayMode::OPENVR_COMPOSITOR_DISTORTED) ||
                (display_mode == DisplayMode::WRAPPER_TO_OPENVR_COMPOSITOR_DISTORTED))
            {
                assert(pose_tracker_openvr);
                pose_tracker_openvr->wait_get_poses();
            }

            //------------------------------------------------------------------------------
            // Grab the HMD pose from the active pose tracker.
            const PoseTracker* active_pose_tracker = nullptr;
            
            switch (pose_tracker_mode) {
            case PoseTrackerMode::UI: active_pose_tracker = pose_tracker_ui.get(); break;
            case PoseTrackerMode::OPENVR: active_pose_tracker = pose_tracker_openvr.get(); break;
            case PoseTrackerMode::WRAPPER: active_pose_tracker = pose_tracker_wrapper.get(); break;
            }

            assert(active_pose_tracker);
            glm::mat4 hmd_pose = active_pose_tracker->hmd_pose();

            //------------------------------------------------------------------------------
            // Select the active/final displays.
            const StereoDisplay* active_stereo_display = nullptr;
            const StereoDisplay* final_stereo_display = nullptr;

            switch (display_mode) {
            case DisplayMode::WINDOW_UNDISTORTED:
                active_stereo_display = window_display.get();
                break;

            case DisplayMode::OPENVR_COMPOSITOR_DISTORTED:
                active_stereo_display = openvr_display.get();
                break;

            case DisplayMode::WRAPPER_TO_WINDOW_DISTORTED:
                active_stereo_display = wrapper_display.get();
                final_stereo_display = window_display.get();
                break;

            case DisplayMode::WRAPPER_TO_OPENVR_COMPOSITOR_DISTORTED:
                active_stereo_display = wrapper_display.get();
                final_stereo_display = openvr_display.get();
                break;
            }

            assert(active_stereo_display);

            //------------------------------------------------------------------------------
            // Grab per-eye transform matrices (these may change at runtime with the IPD).
            std::array<glm::mat4, NUM_EYES> projection_matrices = { glm::mat4(1.0), glm::mat4(1.0) };
            
            if (active_stereo_display) {
                for (size_t eye_index = 0; eye_index < NUM_EYES; ++eye_index) {
                    projection_matrices[eye_index] = active_stereo_display->projection_matrix(eye_index, near_z, far_z);
                }
            }

            //------------------------------------------------------------------------------
            // Render to active display.
            render((*active_stereo_display), hmd_pose, hmd_pose, projection_matrices, fraction);

            //------------------------------------------------------------------------------
            // Submit to display.
            if (final_stereo_display) {
                active_stereo_display->render(*final_stereo_display, time);
            }
            else {
                final_stereo_display = active_stereo_display;
            }

            final_stereo_display->submit();

            //------------------------------------------------------------------------------
            // Advance time/frame.
            time += (1.0 / 90.0);
            render_thread_frame_index = frame_index;
        }

        //------------------------------------------------------------------------------
        // Release current OpenGL context.
        wglMakeCurrent(nullptr, nullptr);
    }

    void create_render_thread()
    {
        std::promise<void> thread_initialized;

        render_thread = std::async(std::launch::async, [&thread_initialized]() {
            SetThreadDescription(GetCurrentThread(), L"Render Thread");

            try {
                initialize_render_thread();
            }
            catch (...) {
                thread_initialized.set_exception(std::current_exception());
                return;
            }

            thread_initialized.set_value();

            try {
                render_loop();
            }
            catch (...) {
                finalize_render_thread();
                throw;
            }

            finalize_render_thread();
        });

        thread_initialized.get_future().get();

        std::cout << "Render thread is running" << std::endl;
    }
    
    void terminate_render_thread()
    {
        if (!render_thread.valid()) {
            return;
        }

        try {
            exit_render_thread = true;
            render_thread.get();

            std::cout << "Render thread terminated" << std::endl;
        }
        catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "Failed to terminate render thread: Unknown exception!" << std::endl;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int
main(int argc, const char* argv[])
{
    std::cout << "vmi-player - Copyright (c) 2019 Mine One GmbH d.b.a ViewMagic. All rights reserved." << std::endl;

    bool enable_wrapper = false;
    bool always_use_openvr_display = false;

    for (int arg_index = 1; arg_index < argc; ++arg_index) {
        // Allow disabling arguments by adding '' in front.
        if (!strcmp(argv[arg_index], "!")) {
            ++arg_index;
        }
        else if ((!strcmp(argv[arg_index], "-?")) || (!strcmp(argv[arg_index], "--help"))) {
            std::cout << std::endl;
            std::cout << "\t-h/--help                     Show command line options." << std::endl;
            std::cout << "\t--enable-wrapper              Use the wrapper library where applicable." << std::endl;
            std::cout << "\t--force-openvr-display        Use the OpenVR display even if a Mosaic display is also available." << std::endl;

            return EXIT_SUCCESS;
        }
        else if (!strcmp(argv[arg_index], "--enable-wrapper")) {
            enable_wrapper = true;
        }
        else if (!strcmp(argv[arg_index], "--force-openvr-display")) {
            always_use_openvr_display = true;
        }
        else {
            std::cerr << "Error: Invalid argument '" << argv[arg_index] << "'!" << std::endl;
            return EXIT_FAILURE;
        }
    }

    //------------------------------------------------------------------------------
    // Initialize NVAPI.
    if (NvAPI_Initialize() != NVAPI_OK) {
        std::cerr << "Warning: Failed to initialize NVAPI!" << std::endl;
    }
    else {
        atexit([]() {
            NvAPI_Unload();
        });
    }

    //------------------------------------------------------------------------------
    // Print interface version string.
    NvAPI_ShortString interface_version = {};

    if (NvAPI_GetInterfaceVersionString(interface_version) == NVAPI_OK) {
        std::cout << "NVAPI interface version: " << interface_version << std::endl;
    }

    //------------------------------------------------------------------------------
    // Initialize GLFW.
    glfwSetErrorCallback([](int error, const char* const description) {
        std::cerr << "GLFW: " << error << ": " << description << std::endl;
    });

    int glfw_version_major = 0;
    int glfw_version_minor = 0;
    int glfw_version_rev = 0;

    glfwGetVersion(&glfw_version_major, &glfw_version_minor, &glfw_version_rev);
    std::cout << "GLFW: " << glfwGetVersionString() << std::endl;

    if ((glfw_version_major < 3) || ((glfw_version_major == 3) && (glfw_version_minor < 3))) {
        std::cerr << "Error: GLFW 3.3 or newer expected!" << std::endl;
        return EXIT_FAILURE;
    }

    if (glfwInit() == GLFW_FALSE) {
        std::cerr << "Error: Failed to initialize GLFW!" << std::endl;
        return EXIT_FAILURE;
    }

    //------------------------------------------------------------------------------
    // Create wrapper (which initialized OpenVR) or initialize OpenVR directly
    // (required for identifying available display configurations below).
    if (enable_wrapper) {
        try {
            wrapper.reset(new HWW::HWWrapper(argc, argv));
        }
        catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }
        catch (...) {
            std::cerr << "Failed to initialize wrapper: Unknown exception!" << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << "Using the wrapper" << std::endl;
    }
    else if (vr::VR_IsRuntimeInstalled() && vr::VR_IsHmdPresent()) {
        vr::EVRInitError error = vr::VRInitError_None;
        vr::IVRSystem* const vr_system = vr::VR_Init(&error, vr::VRApplication_Scene);

        if ((error != vr::VRInitError_None) || !vr_system) {
            std::cerr << "Error: Failed to initialize VR system: " << vr::VR_GetVRInitErrorAsEnglishDescription(error) << std::endl;
            return EXIT_FAILURE;
        }

        atexit([]() {
            vr::VR_Shutdown();
        });

        std::cout << "NOT using the wrapper" << std::endl;
    }

    //------------------------------------------------------------------------------
    // Keep log a bit cleaner by giving the VR system a chance to complete its
    // asynchronous startup before we go on.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    //------------------------------------------------------------------------------
    // Application.
    try {
        //------------------------------------------------------------------------------
        // Get display configuration.
        DisplayConfiguration display_configuration;

        //------------------------------------------------------------------------------
        // Create control window first as we need its context to initialize OpenGL.
        GLFWwindow* const control_window = create_control_window(display_configuration.control_display());

        //------------------------------------------------------------------------------
        // Initialize OpenGL (GLEW) via the control window context.
        glfwMakeContextCurrent(control_window);

        std::cout << "GLEW: " << reinterpret_cast<const char*>(glewGetString(GLEW_VERSION)) << std::endl;

        glGetError();   // Reset OpenGL error.
        const GLenum glew_result = glewInit();

        if (glew_result != GLEW_OK) {
            std::cerr << "Error: Failed to initialize GLEW: " << glewGetErrorString(glew_result) << std::endl;
            return EXIT_FAILURE;
        }

        const GLenum glew_init_opengl_error = glGetError();

        if (glew_init_opengl_error != GL_NO_ERROR) {
            std::cerr << "Warning: GLEW init with OpenGL error: 0x" << std::hex << glew_init_opengl_error << std::dec << std::endl;
        }

        std::cout << "OpenGL vendor: " << glGetString(GL_VENDOR) << std::endl;
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << std::endl;
        std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;

        gl_init_debug_messages();

        //------------------------------------------------------------------------------
        // Create stereo display window and affinity render contexts.
        PIXELFORMATDESCRIPTOR pixel_format_desc = {};
        {
            pixel_format_desc.nSize = sizeof(pixel_format_desc);
            pixel_format_desc.nVersion = 1;

            //------------------------------------------------------------------------------
            // "PFD_DEPTH_DONTCARE: To select a pixel format without a depth buffer, you
            // must specify this flag. The requested pixel format can be with or without a
            // depth buffer. Otherwise, only pixel formats with a depth buffer are
            // considered." [PIXELFORMATDESCRIPTOR documentation]
            pixel_format_desc.dwFlags = (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_DEPTH_DONTCARE);

            //------------------------------------------------------------------------------
            // "For RGBA pixel types, it is the size of the color buffer, excluding the
            // alpha bit planes." [PIXELFORMATDESCRIPTOR documentation]
            pixel_format_desc.iPixelType = PFD_TYPE_RGBA;
            pixel_format_desc.cColorBits = 24;
        };

        //------------------------------------------------------------------------------
        // Select stereo display.
        if (display_configuration.openvr_display() && (always_use_openvr_display || !display_configuration.mosaic_display())) {
            if (IS_DEBUG_BUILD && enable_wrapper) {
                display_mode = DisplayMode::WRAPPER_TO_OPENVR_COMPOSITOR_DISTORTED;
                pose_tracker_mode = PoseTrackerMode::WRAPPER;
            }
            else {
                display_mode = DisplayMode::OPENVR_COMPOSITOR_DISTORTED;
                pose_tracker_mode = PoseTrackerMode::OPENVR;
            }

            stereo_display = display_configuration.openvr_display();

            if (!display_configuration.openvr_display_in_direct_mode()) {
                std::cout << "Using the OpenVR display in extended mode" << std::endl;

                vr::IVRCompositor* const vr_compositor = vr::VRCompositor();

                if (vr_compositor) {
                    vr_compositor->CompositorBringToFront();
                }
            }
            else {
                std::cout << "Using OpenVR display in direct mode" << std::endl;
            }
        }
        else {
            if (enable_wrapper) {
                display_mode = DisplayMode::WRAPPER_TO_WINDOW_DISTORTED;
                pose_tracker_mode = PoseTrackerMode::WRAPPER;
            }
            else {
                display_mode = DisplayMode::WINDOW_UNDISTORTED;
                pose_tracker_mode = PoseTrackerMode::UI;
            }

            stereo_display = display_configuration.mosaic_display();
            stereo_display_window = create_stereo_display_window(stereo_display, pixel_format_desc);

            std::cout << "Using the Mosaic display" << std::endl;
        }

        std::cout << "Stereo display: " << (*stereo_display) << std::endl;

        //------------------------------------------------------------------------------
        // Create render contexts. Required for initializing the stereo display
        // abstraction below.
        DisplayConfiguration::create_render_contexts(
            primary_context,
            support_context,
            stereo_display,
            pixel_format_desc,
            GL_CONTEXT_VERSION_MAJOR,
            GL_CONTEXT_VERSION_MINOR
        );

        std::pair<HDC, HGLRC> window_context(GetDC(stereo_display_window), primary_context.second);

        //------------------------------------------------------------------------------
        // Initialize the stereo display abstraction (which includes initialization of
        // the wrapper in its proper context. The stereo display constructor will make
        // the given display/OpenGL context current so we reset to the control window
        // context at the end.
        switch (display_mode) {
        case DisplayMode::WINDOW_UNDISTORTED:
            window_display.reset(new WindowStereoDisplay(window_context, stereo_display->render_resolution().x, stereo_display->render_resolution().y, ColorSpace::LINEAR, 0.5, 0.060));
            break;

        case DisplayMode::OPENVR_COMPOSITOR_DISTORTED:
            openvr_display.reset(new OpenVRStereoDisplay(primary_context, vr::Submit_Default, 2048, 1024, ColorSpace::LINEAR));
            break;

        case DisplayMode::WRAPPER_TO_WINDOW_DISTORTED:
            wrapper_display.reset(new WrapperStereoDisplay(primary_context, 2048, 1024, ColorSpace::LINEAR, wrapper));
            window_display.reset(new WindowStereoDisplay(window_context, stereo_display->render_resolution().x, stereo_display->render_resolution().y, ColorSpace::LINEAR, 0.5, 0.060));
            break;

        case DisplayMode::WRAPPER_TO_OPENVR_COMPOSITOR_DISTORTED:
            wrapper_display.reset(new WrapperStereoDisplay(primary_context, 2048, 1024, ColorSpace::LINEAR, wrapper));
            openvr_display.reset(new OpenVRStereoDisplay(primary_context, vr::Submit_LensDistortionAlreadyApplied, 2048, 1024, ColorSpace::LINEAR, OpenVRStereoDisplay::CREATE_SINGLE_FRAMEBUFFER_TAG));
            break;
        }

        glfwMakeContextCurrent(control_window);

        //------------------------------------------------------------------------------
        // Now that the wrapper and OpenVR are fully initialized create the pose
        // wrappers before creating the render thread which will use them.
        pose_tracker_ui.reset(new UIPoseTracker());

        if (vr::VRCompositor()) {
            pose_tracker_openvr.reset(new OpenVRPoseTracker());
        }

        if (enable_wrapper) {
            pose_tracker_wrapper.reset(new WrapperPoseTracker(wrapper));
        }

        //------------------------------------------------------------------------------
        // Create render thread.
        create_render_thread();

        //------------------------------------------------------------------------------
        // Run loop.
        glfwSwapInterval(4);
        double time = 0.0;

        while (!glfwWindowShouldClose(control_window)) {
            //------------------------------------------------------------------------------
            // Render control window.
            glClearColor(0.5, 0.25, GLclampf(time), 1.0);
            glClear(GL_COLOR_BUFFER_BIT);

            glfwSwapBuffers(control_window);

            //------------------------------------------------------------------------------
            // Handle events for all windows.
            glfwPollEvents();

            time += (1.0 / 15.0);
            time = fmod(time, 1.0);

            //------------------------------------------------------------------------------
            // Check on control window position/size.
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;

            glfwGetWindowPos(control_window, &x, &y);
            glfwGetWindowSize(control_window, &width, &height);

            if ((x != control_window_x) || (y != control_window_y) || (width != control_window_width) || (height != control_window_height)) {
                control_window_x = x;
                control_window_y = y;
                control_window_width = width;
                control_window_height = height;

                std::cout << "Control window: (" << control_window_x << " / " << control_window_y << ") [" << control_window_width << " x " << control_window_height << "]" << std::endl;
            }

            //------------------------------------------------------------------------------
            // Show render thread progress.
            std::cout << "Render thread frame index: " << render_thread_frame_index << '\r';
        }

        std::cout << std::endl;

        //------------------------------------------------------------------------------
        // Terminate render thread.
        terminate_render_thread();

        //------------------------------------------------------------------------------
        // Finalize wrapper.
        if (wrapper) {
            if (!wglMakeCurrent(primary_context.first, primary_context.second)) {
                throw std::runtime_error("Failed to make OpenGL context current!");
            }

            try {
                glGetError();   // Reset OpenGL error.

                wrapper.reset();

                const GLenum error = glGetError();

                if (error != GL_NO_ERROR) {
                    wrapper_opengl_errors.insert(error);
                }
            }
            catch (std::exception& e) {
                std::cerr << "Exception: " << e.what() << std::endl;
                return EXIT_FAILURE;
            }
            catch (...) {
                std::cerr << "Failed to initialize wrapper: Unknown exception!" << std::endl;
                return EXIT_FAILURE;
            }
        }
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (...) {
        std::cerr << "Application failed: Unknown exception!" << std::endl;
        return EXIT_FAILURE;
    }

    //------------------------------------------------------------------------------
    // List any OpenGL errors that occurred in the wrapper.
    if (!wrapper_opengl_errors.empty()) {
        std::cerr << "Warning: Wrapper had OpenGL errors:" << std::endl;

        for (GLenum error : wrapper_opengl_errors) {
            std::cerr << "  " << toolbox::StlUtils::hex_insert(error) << std::endl;
        }
    }

    //------------------------------------------------------------------------------
    // ...
    return EXIT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
