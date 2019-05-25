
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <array>
#include <cassert>
#include <future>
#include <iostream>
#include <iomanip>
#include <list>
#include <map>
#include <string>
#include <vector>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define GLFW_EXPOSE_NATIVE_WGL
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_NONE

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <GL/glew.h>
#include <GL/wglew.h>
#include <nvapi.h>
#include <cudaGL.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "OpenGLUtilities.h"
#include "main.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

    //------------------------------------------------------------------------------
    // Constants
    //------------------------------------------------------------------------------

    constexpr int GL_CONTEXT_VERSION_MAJOR = 4;
    constexpr int GL_CONTEXT_VERSION_MINOR = 1;

#ifndef NDEBUG
    constexpr int GL_OPENGL_DEBUG_CONTEXT = GLFW_TRUE;
#else // NDEBUG
    constexpr int GL_OPENGL_DEBUG_CONTEXT = GLFW_FALSE;
#endif // NDEBUG

    constexpr char GLSL_VERSION[] = "#version 150";

    //------------------------------------------------------------------------------
    // Utilities
    //------------------------------------------------------------------------------

    void log_last_error_message()
    {
        const DWORD last_error = GetLastError();
        LPSTR last_error_message = nullptr;

        FormatMessageA((FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS),
            NULL, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&last_error_message, 0, NULL);

        fprintf(stderr, "%ju: %s", uintmax_t(last_error), last_error_message);

        LocalFree(last_error_message);
    }

    LRESULT CALLBACK window_callback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if ((uMsg == WM_ERASEBKGND) || (uMsg == WM_PAINT)) {
            if (GetPropA(hWnd, "IGNORE_PAINT_MESSAGES") == nullptr) {
                return DefWindowProc(hWnd, uMsg, wParam, lParam);
            }
            else {
                //------------------------------------------------------------------------------
                // "An application returns zero if it processes this message." [WM_PAINT docs]
                return 0;
            }
        }

        if (uMsg == WM_DISPLAYCHANGE) {
            std::cerr << "Warning: Display change occured. This application is not designed to handle such changes at runtime!" << std::endl;
        }

        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    void glfw_error_callback(int error, const char* const description)
    {
        std::cerr << "GLFW: " << description << std::endl;
    }

    void print_to_stream(std::ostream& stream, const NV_MOSAIC_GRID_TOPO& display_grid, const std::string& indent)
    {
        stream << indent << display_grid.rows << "x" << display_grid.columns << " (" << display_grid.displayCount << (display_grid.displayCount == 1 ? " display) " : " displays) ");
        stream << display_grid.displaySettings.width << "x" << display_grid.displaySettings.height << " @ " << display_grid.displaySettings.freq << " Hz";
        stream << " = " << (display_grid.displaySettings.width * display_grid.columns) << "x" << (display_grid.displaySettings.height * display_grid.rows) << std::endl;

        for (size_t r = 0; r < display_grid.rows; ++r) {
            for (size_t c = 0; c < display_grid.columns; ++c) {
                const NvU32 display_id = display_grid.displays[c + (r * display_grid.columns)].displayId;

                std::cout << indent << "[" << r << "," << c << "] 0x" << std::hex << std::setfill('0') << std::setw(8) << display_id << std::dec;
            }
        }
    }

    void print_display_flags_to_stream(std::ostream& stream, DWORD flags)
    {
        stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << flags << std::dec;

        if (flags != 0) {
            bool first = true;

            if ((flags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
                if (first) { stream << " ("; }
                else { stream << ", "; }
                stream << "display attached";
                first = false;
            }

            if ((flags & DISPLAY_DEVICE_PRIMARY_DEVICE) == DISPLAY_DEVICE_PRIMARY_DEVICE) {
                if (first) { stream << " ("; }
                else { stream << ", "; }
                stream << "primary display";
                first = false;
            }

            if ((flags & DISPLAY_DEVICE_UNSAFE_MODES_ON) == DISPLAY_DEVICE_UNSAFE_MODES_ON) {
                if (first) { stream << " ("; }
                else { stream << ", "; }
                stream << "unsafe modes on";
                first = false;
            }

            if (!first) { stream << ")"; }
        }
    }

    void create_texture_backed_render_targets(GLuint* const framebuffers,
        GLuint* const color_attachments,
        size_t n,
        size_t width,
        size_t height)
    {
        glGenFramebuffers(GLsizei(n), framebuffers);
        glGenTextures(GLsizei(n), color_attachments);

        for (size_t i = 0; i < n; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
            glBindTexture(GL_TEXTURE_2D, color_attachments[i]);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_attachments[i], 0);

            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

            if (status != GL_FRAMEBUFFER_COMPLETE) {
                throw std::runtime_error("Failed to validate framebuffer status!");
            }
        }
    }

    void delete_texture_backed_render_targets(const GLuint* const framebuffers, const GLuint* const color_attachments, size_t n)
    {
        glDeleteTextures(GLsizei(n), color_attachments);
        glDeleteFramebuffers(GLsizei(n), framebuffers);
    }

    std::mutex render_threads_mutex;
    std::list<std::future<void>> render_threads; 
    std::condition_variable start_render_threads_event;
    bool start_render_threads_flag = false;

    void start_render_threads(std::vector<HDC> display_contexts, std::vector<HGLRC> gl_contexts, const std::function<void(size_t)>& initialize, const std::function<void(size_t)>& render)
    {
        //------------------------------------------------------------------------------
        // Check arguments.
        assert(display_contexts.size() == gl_contexts.size());

        //------------------------------------------------------------------------------
        // Synchronize this function.
        std::unique_lock<std::mutex> lock(render_threads_mutex);
        start_render_threads_flag = false;

        //------------------------------------------------------------------------------
        // Start all threads and let them do their setup.
        for (size_t thread_index = 0; thread_index < display_contexts.size(); ++thread_index) {
            std::cout << "Starting render thread " << thread_index << std::endl;

            const HDC display_context = display_contexts[thread_index];
            const HGLRC gl_context = gl_contexts[thread_index];
            std::promise<void> render_thread_ready;

            std::future<void> render_thread = std::async(std::launch::async, [&render_thread_ready](
                HDC display_context,
                HGLRC gl_context,
                const std::function<void(size_t)>& initialize,
                const std::function<void(size_t)>& render,
                size_t thread_index)
            {
                try {
                    //------------------------------------------------------------------------------
                    // Prepare for rendering.
                    if (wglMakeCurrent(display_context, gl_context) != TRUE) {
                        std::cerr << "Error: Failed to make OpenGL context current: ";
                        log_last_error_message();
                        throw std::runtime_error("Failed to make OpenGL context current!");
                    }

                    //------------------------------------------------------------------------------
                    // Check associated CUDA device.
                    unsigned int cuda_device_count = 0;
                    std::array<CUdevice, 4> cuda_devices;

                    if (cuGLGetDevices(&cuda_device_count, cuda_devices.data(), unsigned int(cuda_devices.size()), CU_GL_DEVICE_LIST_ALL) == CUDA_SUCCESS) {
                        for (size_t i = 0; i < cuda_device_count; ++i) {
                            std::cout << "  CUDA device: " << cuda_devices[i] << std::endl;
                        }
                    }

                    //------------------------------------------------------------------------------
                    // Initialize.
                    initialize(thread_index);

                    //------------------------------------------------------------------------------
                    // Signal we are ready for rendering then wait for signal to start rendering.
                    render_thread_ready.set_value();

                    {
                        std::unique_lock<std::mutex> lock(render_threads_mutex);
                        start_render_threads_event.wait(lock, []() { return start_render_threads_flag; });
                    }

                    //------------------------------------------------------------------------------
                    // Render.
                    render(thread_index);
                }
                catch (std::exception& e) {
                    std::cerr << "Exception: " << e.what() << std::endl;
                }
                catch (...) {
                    std::cerr << "Exception: <unknown>!" << std::endl;
                }
            }, display_context, gl_context, initialize, render, thread_index);

            //------------------------------------------------------------------------------
            // Wait for the thread to be ready for rendering.
            render_thread_ready.get_future().wait();
            render_threads.emplace_back(std::move(render_thread));
        }

        //------------------------------------------------------------------------------
        // Signal all render threads to start.
        start_render_threads_flag = true;
        start_render_threads_event.notify_all();
    }

    void join_render_thread(std::future<void>& render_thread)
    {
        try {
            render_thread.get();
        }
        catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "Exception: <unknown>!" << std::endl;
        }
    }

    void join_render_threads()
    {
        std::unique_lock<std::mutex> lock(render_threads_mutex);

        for (auto it = begin(render_threads); it != end(render_threads); ++it) {
            if (it->valid() == false) {
                continue;
            }

            join_render_thread(*it);
        }

        render_threads.clear();
    }

    bool try_join_render_threads(size_t timeout)
    {
        std::unique_lock<std::mutex> lock(render_threads_mutex);

        for (auto it = begin(render_threads); it != end(render_threads);) {
            if (it->valid() == false) {
                it = render_threads.erase(it);
                continue;
            }

            if (it->wait_for(std::chrono::milliseconds(timeout)) == std::future_status::ready) {
                join_render_thread(*it);
                it = render_threads.erase(it);
                continue;
            }

            ++it;
        }

        return render_threads.empty();
    }

    class RenderPoints
    {
    public:

        static GLuint create_program()
        {
            static const char* const vs_string =
                "#version 410\n"
                "uniform vec4 u_rect;\n"
                "uniform mat4 u_mvp;\n"
                "out vec2 v_uv;\n"
                "void main() {\n"
                "    int x = (gl_VertexID % 1024);\n"
                "    int y = (gl_VertexID / 1024);\n"
                "    vec2 uv = (vec2(x, y) * (1.0 / 1023.0));\n"
                "    gl_Position = (u_mvp * vec4((u_rect.xy + (uv * u_rect.zw)), 0.0, 1.0));\n"
                "    v_uv = vec2(uv.x, uv.y);\n"
                "}\n";

            static const char* const fs_string =
                "#version 410\n"
                "in vec2 v_uv;\n"
                "out vec4 f_color;\n"
                "void main() {\n"
                "    float vignette = pow(clamp(((v_uv.x * (1.0f - v_uv.x)) * (v_uv.y * (1.0f - v_uv.y)) * 36.0f), 0.0, 1.0), 4.0);\n"
                "    f_color = vec4((v_uv.rg * vignette), 0.0, 1.0);\n"
                "}\n";

            try {
                const GLuint vertex_shader = toolbox::OpenGLShader::create_from_source(GL_VERTEX_SHADER, vs_string);
                const GLuint fragment_shader = toolbox::OpenGLShader::create_from_source(GL_FRAGMENT_SHADER, fs_string);

                toolbox::OpenGLProgram::attribute_location_list_t attribute_locations;
                toolbox::OpenGLProgram::frag_data_location_list_t frag_data_locations;
                const GLuint program = toolbox::OpenGLProgram::create_from_shaders(vertex_shader, fragment_shader, attribute_locations, frag_data_locations);

                s_uniform_location_rect = glGetUniformLocation(program, "u_rect");
                s_uniform_location_mvp = glGetUniformLocation(program, "u_mvp");

                return program;
            }
            catch (std::exception& e) {
                std::cerr << "Exception: " << e.what() << std::endl;
            }
            catch (...) {
                std::cerr << "Exception: <unknown>!" << std::endl;
            }

            return 0;
        }

        static void set_rect(const float* const ndc_rect)
        {
            if (s_uniform_location_rect != -1) {
                glUniform4fv(s_uniform_location_rect, 1, ndc_rect);
            }
        }

        static void set_mvp(const float* const mvp)
        {
            if (s_uniform_location_mvp != -1) {
                glUniformMatrix4fv(s_uniform_location_mvp, 1, GL_FALSE, mvp);
            }
        }

        static void draw(GLuint& vao)
        {
            if (!vao) {
                glGenVertexArrays(1, &vao);
            }

            glBindVertexArray(vao);
            glDrawArrays(GL_POINTS, 0, (1024 * 1024));
        }

    private:

        static GLint        s_uniform_location_rect;
        static GLint        s_uniform_location_mvp;
    };

    GLint RenderPoints::s_uniform_location_rect = -1;
    GLint RenderPoints::s_uniform_location_mvp = -1;

    //------------------------------------------------------------------------------
    // Global data.
    //------------------------------------------------------------------------------

    float rect[4] = { -1.0, -1.0, 2.0, 2.0 };

    float mvp[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };

    uint8_t pixels[4][64 * 64 * 4];

    //------------------------------------------------------------------------------
    // DisplayConfiguration
    //------------------------------------------------------------------------------

    typedef struct rect_s {
        long        m_x = 0;
        long        m_y = 0;
        long        m_width = 0;
        long        m_height = 0;

        rect_s() {}
        rect_s(long x, long y, long w, long h) : m_x(x), m_y(y), m_width(w), m_height(h) {}
    } rect_t;

    class Display
    {
    public:

        Display(std::string name, const rect_t& virtual_screen_rect)
            : m_name(std::move(name))
            , m_virtual_screen_rect(virtual_screen_rect)
        {
            if (m_name.empty()) {
                throw std::runtime_error("Valid name expected!");
            }

            if ((m_virtual_screen_rect.m_width == 0) || (m_virtual_screen_rect.m_height == 0)) {
                throw std::runtime_error("Valid virtual screen rect expected!");
            }
        }

        bool valid_non_mosaic() const noexcept {
            if ((m_nv_display_id == 0) || (m_nv_display_handle == nullptr)) {
                return false;
            }

            if ((m_nv_num_physical_gpus != 1) || (m_nv_mosaic_num_displays != 1)) {
                return false;
            }

            return true;
        }

        bool valid_mosaic() const noexcept {
            if ((m_nv_display_id == 0) || (m_nv_display_handle == nullptr)) {
                return false;
            }

            if ((m_nv_num_physical_gpus < 1) || (m_nv_mosaic_num_displays < 2)) {
                return false;
            }

            return true;
        }

    public:

        const std::string& name() const noexcept { return m_name; }
        const rect_t& virtual_screen_rect() const noexcept { return m_virtual_screen_rect; }

        NvU32 nv_display_id() const noexcept { return m_nv_display_id; }
        NvDisplayHandle nv_display_handle() const noexcept { return m_nv_display_handle; }
        size_t nv_num_physical_gpus() const noexcept { return m_nv_num_physical_gpus; }

        void set_nv_display(NvU32 nv_display_id, NvDisplayHandle nv_display_handle, size_t nv_num_physical_gpus) {
            m_nv_display_id = nv_display_id;
            m_nv_display_handle = nv_display_handle;
            m_nv_num_physical_gpus = nv_num_physical_gpus;
        }

        size_t nv_mosaic_num_displays() const noexcept { return m_nv_mosaic_num_displays; }
        void set_nv_mosaic_num_displays(size_t nv_mosaic_num_displays) { m_nv_mosaic_num_displays = nv_mosaic_num_displays; }

    private:

        const std::string       m_name;
        const rect_t            m_virtual_screen_rect;

        NvU32                   m_nv_display_id = 0;
        NvDisplayHandle         m_nv_display_handle = nullptr;
        size_t                  m_nv_num_physical_gpus = 0;

        size_t                  m_nv_mosaic_num_displays = 0;
    };

    class DisplayConfiguration
    {
    public:

        DisplayConfiguration()
        {
            //------------------------------------------------------------------------------
            // Get a list of physical displays (monitors) from Windows.
            //------------------------------------------------------------------------------

            //------------------------------------------------------------------------------
            // Get the Virtual Screen geometry.
            const rect_t virtual_screen(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
            const size_t num_virtual_screen_monitors = GetSystemMetrics(SM_CMONITORS);

            std::cout << "Virtual Screen origin: " << virtual_screen.m_x << " / " << virtual_screen.m_y << std::endl;
            std::cout << "Virtual Screen size: " << virtual_screen.m_width << " x " << virtual_screen.m_height << std::endl;
            std::cout << "Virtual Screen spans " << num_virtual_screen_monitors << " monitor(s)" << std::endl;

            //------------------------------------------------------------------------------
            // Enumerate physical displays, each represented by a HMONITOR handle. 
            if (EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC display_context, LPRECT virtual_screen_rect_, LPARAM user_data) {
                DisplayConfiguration* const this_ = reinterpret_cast<DisplayConfiguration*>(user_data);

                //------------------------------------------------------------------------------
                // Evaluate virtual screen rectangle.
                rect_t virtual_screen_rect = {};
                {
                    virtual_screen_rect.m_x = virtual_screen_rect_->left;
                    virtual_screen_rect.m_y = virtual_screen_rect_->top;
                    virtual_screen_rect.m_width = (virtual_screen_rect_->right - virtual_screen_rect_->left);
                    virtual_screen_rect.m_height = (virtual_screen_rect_->bottom - virtual_screen_rect_->top);
                }

                //------------------------------------------------------------------------------
                // Get additional monitor info.
                MONITORINFOEX monitor_info = {};
                monitor_info.cbSize = sizeof(monitor_info);

                bool is_primary = false;

                if (GetMonitorInfo(monitor, &monitor_info) != 0) {
                    this_->m_displays.emplace_back(new Display(monitor_info.szDevice, virtual_screen_rect));

                    std::cout << "Monitor " << monitor_info.szDevice << ": ";
                    is_primary = ((monitor_info.dwFlags & MONITORINFOF_PRIMARY) == MONITORINFOF_PRIMARY);
                }
                else {
                    std::cout << "Monitor 0x" << monitor << ": ";
                }

                std::cout << "(" << virtual_screen_rect.m_x << " / " << virtual_screen_rect.m_y << ") [" << virtual_screen_rect.m_width << " x " << virtual_screen_rect.m_height << "]";

                if (is_primary) {
                    std::cout << " (primary)";
                }

                std::cout << std::endl;

                //------------------------------------------------------------------------------
                // ...
                return TRUE;
            }, LPARAM(this)) == 0)
            {
                throw std::runtime_error("Failed to enumerate monitors!");
            }

            //------------------------------------------------------------------------------
            // Get Mosaic information for displays.
            //------------------------------------------------------------------------------

            //------------------------------------------------------------------------------
            // Enumerate displays and note NVIDIA display IDs which are required below.
            NvDisplayHandle display_handle = nullptr;
            NvU32 display_index = 0;

            while (NvAPI_EnumNvidiaDisplayHandle(display_index, &display_handle) == NVAPI_OK) {
                NvAPI_ShortString display_name = {};

                if (NvAPI_GetAssociatedNvidiaDisplayName(display_handle, display_name) == NVAPI_OK) {
                    const auto it = std::find_if(begin(m_displays), end(m_displays), [display_name](const std::shared_ptr<Display>& display) {
                        return (display->name() == display_name);
                    });

                    if (it == end(m_displays)) {
                        std::cout << "NVAPI enunmerates display " << display_name << " but Windows does not!" << std::endl;
                    }
                    else {
                        NvU32 display_id = 0;

                        if (NvAPI_DISP_GetDisplayIdByDisplayName(display_name, &display_id) == NVAPI_OK) {                            
                            NvPhysicalGpuHandle physical_gpus[NVAPI_MAX_PHYSICAL_GPUS] = {};
                            NvU32 num_physical_gpus = 0;

                            if (NvAPI_GetPhysicalGPUsFromDisplay(display_handle, physical_gpus, &num_physical_gpus) != NVAPI_OK) {
                                throw std::runtime_error("Failed to get physical GPU count!");
                            }

                            (*it)->set_nv_display(display_id, display_handle, num_physical_gpus);
                        }
                    }
                }

                ++display_index;
            }

            //------------------------------------------------------------------------------
            // Get brief of current mosaic topology.
            NV_MOSAIC_TOPO_BRIEF mosaic_topology = {};
            mosaic_topology.version = NVAPI_MOSAIC_TOPO_BRIEF_VER;

            NV_MOSAIC_DISPLAY_SETTING mosaic_display_settings = {};
            mosaic_display_settings.version = NVAPI_MOSAIC_DISPLAY_SETTING_VER;

            NvS32 mosaic_overlap_x = 0;
            NvS32 mosaic_overlap_y = 0;

            if (NvAPI_Mosaic_GetCurrentTopo(&mosaic_topology, &mosaic_display_settings, &mosaic_overlap_x, &mosaic_overlap_y) != NVAPI_OK) {
                throw std::runtime_error("Failed to get mosaic topology!");
            }

            //------------------------------------------------------------------------------
            // If a topology is enabled show which one.
            if (!mosaic_topology.enabled) {
                if (mosaic_topology.isPossible) {
                    throw std::runtime_error("Mosaic is DISABLED (but possible)!");
                }
                else {
                    throw std::runtime_error("Mosaic is DISABLED!");
                }
            }

            std::cout << "Mosaic is ENABLED: ";

            switch (mosaic_topology.topo) {
            case NV_MOSAIC_TOPO_1x2_BASIC: std::cout << "1x2"; break;
            case NV_MOSAIC_TOPO_2x1_BASIC: std::cout << "2x1"; break;
            case NV_MOSAIC_TOPO_1x3_BASIC: std::cout << "1x3"; break;
            case NV_MOSAIC_TOPO_3x1_BASIC: std::cout << "3x1"; break;
            case NV_MOSAIC_TOPO_1x4_BASIC: std::cout << "1x4"; break;
            case NV_MOSAIC_TOPO_4x1_BASIC: std::cout << "4x1"; break;
            case NV_MOSAIC_TOPO_2x2_BASIC: std::cout << "2x2"; break;
            case NV_MOSAIC_TOPO_2x3_BASIC: std::cout << "2x3"; break;
            case NV_MOSAIC_TOPO_2x4_BASIC: std::cout << "2x4"; break;
            case NV_MOSAIC_TOPO_3x2_BASIC: std::cout << "3x2"; break;
            case NV_MOSAIC_TOPO_4x2_BASIC: std::cout << "4x2"; break;
            case NV_MOSAIC_TOPO_1x5_BASIC: std::cout << "1x5"; break;
            case NV_MOSAIC_TOPO_1x6_BASIC: std::cout << "1x6"; break;
            case NV_MOSAIC_TOPO_7x1_BASIC: std::cout << "1x7"; break;
            case NV_MOSAIC_TOPO_1x2_PASSIVE_STEREO: std::cout << "1x2 passive stereo"; break;
            case NV_MOSAIC_TOPO_2x1_PASSIVE_STEREO: std::cout << "2x1 passive stereo"; break;
            case NV_MOSAIC_TOPO_1x3_PASSIVE_STEREO: std::cout << "1x3 passive stereo"; break;
            case NV_MOSAIC_TOPO_3x1_PASSIVE_STEREO: std::cout << "3x1 passive stereo"; break;
            case NV_MOSAIC_TOPO_1x4_PASSIVE_STEREO: std::cout << "1x4 passive stereo"; break;
            case NV_MOSAIC_TOPO_4x1_PASSIVE_STEREO: std::cout << "4x1 passive stereo"; break;
            case NV_MOSAIC_TOPO_2x2_PASSIVE_STEREO: std::cout << "2x2 passive stereo"; break;
            default: std::cout << "unknown topology"; break;
            }

            std::cout << ", overlap (" << mosaic_overlap_x << ", " << mosaic_overlap_y << ")" << std::endl;

            //------------------------------------------------------------------------------
            // Show current display grid (mosaic) configuration, including where mosaic is
            // disabled and each display is a 1x1 grid.
            NvU32 num_grids = 0;

            if (NvAPI_Mosaic_EnumDisplayGrids(nullptr, &num_grids) != NVAPI_OK) {
                throw std::runtime_error("Failed to enumerate display grids!");
            }

            std::vector<NV_MOSAIC_GRID_TOPO> display_grids(num_grids);

            std::for_each(begin(display_grids), end(display_grids), [](NV_MOSAIC_GRID_TOPO& display_grid) {
                display_grid.version = NV_MOSAIC_GRID_TOPO_VER;
            });

            if (NvAPI_Mosaic_EnumDisplayGrids(display_grids.data(), &num_grids) != NVAPI_OK) {
                throw std::runtime_error("Failed to enumerate display grids!");
            }

            //------------------------------------------------------------------------------
            // In some cases the initially reported number appears to be conservative so
            // we trim the vector down to the actual size in case.
            assert(display_grids.size() >= num_grids);
            display_grids.resize(num_grids);

            //------------------------------------------------------------------------------
            // Grab information relevant to our display configuration.
            size_t display_grid_index = 0;

            for (const NV_MOSAIC_GRID_TOPO& display_grid : display_grids) {
                const NvU32 first_display_id = display_grid.displays[0].displayId;

                const auto it = std::find_if(begin(m_displays), end(m_displays), [first_display_id](const std::shared_ptr<Display>& display) {
                    return (display->nv_display_id() == first_display_id);
                });

                if (it == end(m_displays)) {
                    std::cout << "NVAPI enunmerates display " << first_display_id << " but Windows does not!" << std::endl;
                }
                else {
                    (*it)->set_nv_mosaic_num_displays(display_grid.displayCount);
                }

                //------------------------------------------------------------------------------
                // Print display grid info to console.
                std::cout << "Display Grid " << display_grid_index++ << std::endl;
                print_to_stream(std::cout, display_grid, "  ");
            }

            //------------------------------------------------------------------------------
            // Select displays.
            size_t max_num_displays = 1;

            for (const auto& display : m_displays) {
                if (!m_control_display && display->valid_non_mosaic()) {
                    m_control_display = display;
                }
                else if (display->valid_mosaic() && (display->nv_mosaic_num_displays() > max_num_displays)) {
                    m_mosaic_display = display;
                    max_num_displays = display->nv_mosaic_num_displays();
                }
                else {
                    throw std::runtime_error("Invalid display!");
                }
            }

            if (!m_control_display) {
                throw std::runtime_error("No valid control display available!");
            }

            if (!m_mosaic_display) {
                throw std::runtime_error("No valid mosaic display available!");
            }
        }

     public:

         std::shared_ptr<Display> control_display() const { return m_control_display; }
         std::shared_ptr<Display> mosaic_display() const { return m_mosaic_display; }

    private:

        std::vector<std::shared_ptr<Display>>       m_displays;

        std::shared_ptr<Display>                    m_control_display;
        std::shared_ptr<Display>                    m_mosaic_display;
    };

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void
control_window_key_callback(GLFWwindow* const window, int key, int scancode, int action, int mods)
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

GLFWwindow*
create_control_window(std::shared_ptr<Display> control_display)
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
    GLFWwindow* const control_window = glfwCreateWindow((control_display->virtual_screen_rect().m_width - 200), (control_display->virtual_screen_rect().m_height - 200), "VMI Player", NULL, NULL);

    if (!control_window) {
        throw std::runtime_error("Failed to create main window!");
    }

    glfwSetKeyCallback(control_window, &control_window_key_callback);
    glfwSetWindowPos(control_window, (control_display->virtual_screen_rect().m_x + 100), (control_display->virtual_screen_rect().m_y + 100));
    glfwShowWindow(control_window);

    //------------------------------------------------------------------------------
    // Make the control display contexts current so we can initialize OpenGL (GLEW).
    glfwMakeContextCurrent(control_window);

    std::cout << "OpenGL vendor: " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;

    const GLenum glew_result = glewInit();

    if (glew_result != GLEW_OK) {
        std::string e = "Failed to initialize GLEW: ";
        e.append(reinterpret_cast<const char*>(glewGetErrorString(glew_result)));

        throw std::runtime_error(e);
    }

    //------------------------------------------------------------------------------
    // ...
    return control_window;
}

HWND
create_mosaic_window(std::shared_ptr<Display> mosaic_display)
{
    //------------------------------------------------------------------------------
    // Register a window class.
    WNDCLASSA wc = {};
    {
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = window_callback;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = NULL;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "VMI Player";
    }

    RegisterClassA(&wc);

    //------------------------------------------------------------------------------
    // "An OpenGL window should be created with the WS_CLIPCHILDREN and
    // WS_CLIPSIBLINGS styles. Additionally, the window class attribute should NOT
    // include the CS_PARENTDC style." [SetPixelFormat documentation]
    const DWORD style = (WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

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
        // alpha bitplanes." [PIXELFORMATDESCRIPTOR documentation]
        pixel_format_desc.iPixelType = PFD_TYPE_RGBA;
        pixel_format_desc.cColorBits = 24;
    };

    //------------------------------------------------------------------------------
    // Create a 'full screen' window.
    RECT window_rect = {};
    {
        SetRect(&window_rect,
            mosaic_display->virtual_screen_rect().m_x,
            mosaic_display->virtual_screen_rect().m_y,
            (mosaic_display->virtual_screen_rect().m_x + mosaic_display->virtual_screen_rect().m_width),
            (mosaic_display->virtual_screen_rect().m_y + mosaic_display->virtual_screen_rect().m_height));

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
    SetPropA(window, "IGNORE_PAINT_MESSAGES", HANDLE(1));

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HGLRC
create_opengl_affinity_context(HDC& affinity_display_context, HGPUNV gpu, const PIXELFORMATDESCRIPTOR& pixel_format_desc)
{
    //------------------------------------------------------------------------------
    // Create and setup affinity display context.
    const HGPUNV gpu_list[2] = { gpu, nullptr };
    
    affinity_display_context = wglCreateAffinityDCNV(gpu_list);

    if (affinity_display_context == nullptr) {
        throw std::runtime_error("Failed to create affinity display context!");
    }

    const int pixel_format = ChoosePixelFormat(affinity_display_context, &pixel_format_desc);

    if (pixel_format == 0) {
        wglDeleteDCNV(affinity_display_context);
        throw std::runtime_error("Failed to choose pixel format!");
    }

    if (SetPixelFormat(affinity_display_context, pixel_format, &pixel_format_desc) != TRUE) {
        wglDeleteDCNV(affinity_display_context);
        throw std::runtime_error("Failed to set pixel format!");
    }

    if ((0)) {
        PIXELFORMATDESCRIPTOR pixel_format_desc = {};
        {
            pixel_format_desc.nSize = sizeof(pixel_format_desc);
            pixel_format_desc.nVersion = 1;
        };

        if (DescribePixelFormat(affinity_display_context, pixel_format, pixel_format_desc.nSize, &pixel_format_desc) == 0) {
            wglDeleteDCNV(affinity_display_context);
            throw std::runtime_error("Failed to describe pixel format!");
        }
    }

    //------------------------------------------------------------------------------
    // Ccreate OpenGL affinity context.
    const int attrib_list[] = {
        //WGL_CONTEXT_MAJOR_VERSION_ARB, GL_CONTEXT_VERSION_MAJOR,
        //WGL_CONTEXT_MINOR_VERSION_ARB, GL_CONTEXT_VERSION_MINOR,
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
        0
    };
    
    const HGLRC gl_context = wglCreateContextAttribsARB(affinity_display_context, nullptr, attrib_list);

    if (gl_context == nullptr) {
        wglDeleteDCNV(affinity_display_context);
        throw std::runtime_error("Failed to create OpenGL context!");
    }

    //------------------------------------------------------------------------------
    // ...
    return gl_context;
}

HDC primary_dc = nullptr;
HGLRC primary_gl_context = nullptr;

HDC support_dc = nullptr;
HGLRC support_gl_context = nullptr;

void
create_render_contexts(const DisplayConfiguration& display_configuration)
{
    //------------------------------------------------------------------------------
    // Identify primary/support GPUs.
    HGPUNV primary_gpu = nullptr;
    HGPUNV support_gpu = nullptr;

    UINT gpu_index = 0;
    HGPUNV gpu = nullptr;

    std::vector<HGPUNV> gpus;

    while (wglEnumGpusNV(gpu_index, &gpu)) {
        std::cout << "GPU " << gpu_index << ":" << std::endl;
        gpus.push_back(gpu);

        //------------------------------------------------------------------------------
        // Enumerate devices (displays).
        GPU_DEVICE gpu_device;
        gpu_device.cb = sizeof(gpu_device);

        for (UINT device_index = 0; wglEnumGpuDevicesNV(gpu, device_index, &gpu_device); ++device_index) {
            if ((gpu_device.Flags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
                continue;
            }

            std::cout << "  Device " << device_index << ": ";
            std::cout << gpu_device.DeviceString << ", " << gpu_device.DeviceName << ", ";
            print_display_flags_to_stream(std::cout, gpu_device.Flags);
            std::cout << std::endl;

            if (display_configuration.mosaic_display()->name() == gpu_device.DeviceName) {
                primary_gpu = gpu;
            }
            else if (!support_gpu) {
                support_gpu = gpu;
            }
        }

        ++gpu_index;
    }

    if (!primary_gpu && !support_gpu) {
        throw std::runtime_error("Failed to identify primary or support GPU!");
    }

    //------------------------------------------------------------------------------
    // No specific pixel format is required for an affinity (display) context as it
    // does not have a default framebuffer. 
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
        // alpha bitplanes." [PIXELFORMATDESCRIPTOR documentation]
        pixel_format_desc.iPixelType = PFD_TYPE_RGBA;
        pixel_format_desc.cColorBits = 24;
    };

    //------------------------------------------------------------------------------
    // Create the OpenGL affinity contexts and share lists between them.
    primary_gl_context = create_opengl_affinity_context(primary_dc, primary_gpu, pixel_format_desc);
    support_gl_context = create_opengl_affinity_context(support_dc, support_gpu, pixel_format_desc);

    if (wglShareLists(primary_gl_context, support_gl_context)) {
        throw std::runtime_error("Failed to share OpenGL lists!");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    //------------------------------------------------------------------------------
    // Initialize NVAPI (and make sure NvAPI_Unload is called under exceptions).
    if (NvAPI_Initialize() != NVAPI_OK) {
        throw std::runtime_error("Failed to initialize NVAPI!");
    }

    std::shared_ptr<uint8_t> nvapi_unload(reinterpret_cast<uint8_t*>(0), [](uint8_t*) {
        NvAPI_Unload();
    });

    //------------------------------------------------------------------------------
    // Print interface version string.
    NvAPI_ShortString interface_version = {};

    if (NvAPI_GetInterfaceVersionString(interface_version) == NVAPI_OK) {
        std::cout << "NVAPI interface version: " << interface_version << std::endl;
    }

    //------------------------------------------------------------------------------
    // Initialize GLFW.
    glfwSetErrorCallback(&glfw_error_callback);

    int glfw_version_major = 0;
    int glfw_version_minor = 0;
    int glfw_version_rev = 0;

    glfwGetVersion(&glfw_version_major, &glfw_version_minor, &glfw_version_rev);
    std::cout << "GLFW: " << glfwGetVersionString() << std::endl;

    if ((glfw_version_major < 3) || ((glfw_version_major == 3) && (glfw_version_minor < 2))) {
        std::cerr << "Error: GLFW 3.2 or newer exepcted!" << std::endl;
        return EXIT_FAILURE;
    }

    if (glfwInit() == GLFW_FALSE) {
        std::cerr << "Error: Failed to initialize GLFW!" << std::endl;
        return EXIT_FAILURE;
    }

    //------------------------------------------------------------------------------
    // Get display configuration.
    try {
        DisplayConfiguration display_configuration;
        
        GLFWwindow* const control_window = create_control_window(display_configuration.control_display());
        const HWND mosaic_window = create_mosaic_window(display_configuration.mosaic_display());
        create_render_contexts(display_configuration);

        MSG message = {};
        float time = 0.0;

        while (!glfwWindowShouldClose(control_window)) {
            if ((0)) {
                wglMakeCurrent(support_dc, support_gl_context);
                wglMakeCurrent(primary_dc, primary_gl_context);
            }

            if ((1)) {
                const HDC mosaic_dc = GetDC(mosaic_window);

                if (wglMakeCurrent(mosaic_dc, primary_gl_context)) {
                    glClearColor(0.25, 0.5, time, 1.0);
                    glClear(GL_COLOR_BUFFER_BIT);
                    SwapBuffers(mosaic_dc);
                }

                ReleaseDC(mosaic_window, mosaic_dc);
            }

            glfwMakeContextCurrent(control_window);
            glClearColor(0.5, 0.25, time, 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
            
            glfwSwapBuffers(control_window);
            glfwPollEvents();

            time += (1.0 / 30.0);
            time = fmodf(time, 1.0);
        }
    }
    catch (std::exception& e) {
        std::cerr << "Error: Not a valid display configuration: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
