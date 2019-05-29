
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// STL
//------------------------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <thread>

//------------------------------------------------------------------------------
// Platform
//------------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <dxgi1_6.h>
#include <wrl.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// NVAPI
//------------------------------------------------------------------------------

#include <nvapi.h>

//------------------------------------------------------------------------------
// GLFW
//------------------------------------------------------------------------------

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

//------------------------------------------------------------------------------
// GLEW
//------------------------------------------------------------------------------

#include <GL/glew.h>
#include <GL/wglew.h>

//------------------------------------------------------------------------------
// OpenVR
//------------------------------------------------------------------------------

#include <openvr.h>

//------------------------------------------------------------------------------
// Toolbox
//------------------------------------------------------------------------------

#include "OpenGLUtilities.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    //------------------------------------------------------------------------------
    // Constants
    //------------------------------------------------------------------------------

    constexpr int GL_CONTEXT_VERSION_MAJOR = 4;
    constexpr int GL_CONTEXT_VERSION_MINOR = 5;

#ifndef NDEBUG
    constexpr int GL_OPENGL_DEBUG_CONTEXT = GLFW_TRUE;
#else // NDEBUG
    constexpr int GL_OPENGL_DEBUG_CONTEXT = GLFW_FALSE;
#endif // NDEBUG

    constexpr char GLSL_VERSION[] = "#version 450";

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    //------------------------------------------------------------------------------
    // Utilities
    //------------------------------------------------------------------------------

    const std::string NO_INDENT;

    void print_to_stream(std::ostream& stream, const NV_MOSAIC_GRID_TOPO& display_grid, const std::string& indent = NO_INDENT)
    {
        stream << indent << display_grid.rows << "x" << display_grid.columns << " (" << display_grid.displayCount << (display_grid.displayCount == 1 ? " display) " : " displays) ");
        stream << display_grid.displaySettings.width << "x" << display_grid.displaySettings.height << " @ " << display_grid.displaySettings.freq << " Hz";
        stream << " = " << (display_grid.displaySettings.width * display_grid.columns) << "x" << (display_grid.displaySettings.height * display_grid.rows) << std::endl;

        for (size_t r = 0; r < display_grid.rows; ++r) {
            for (size_t c = 0; c < display_grid.columns; ++c) {
                const NvU32 display_id = display_grid.displays[c + (r * display_grid.columns)].displayId;

                stream << indent << "[" << r << "," << c << "] 0x" << std::hex << std::setfill('0') << std::setw(8) << display_id << std::dec;
            }
        }

        stream << std::endl;
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

    //------------------------------------------------------------------------------
    // rect_t
    //------------------------------------------------------------------------------

    typedef struct rect_s {
        long        m_x = 0;
        long        m_y = 0;
        long        m_width = 0;
        long        m_height = 0;

        rect_s() {}
        rect_s(long x, long y, long w, long h) : m_x(x), m_y(y), m_width(w), m_height(h) {}

        bool operator==(const rect_s& rhs) const noexcept {
            return ((rhs.m_x == m_x) && (rhs.m_y == m_y) && (rhs.m_width == m_width) && (rhs.m_height == m_height));
        }

        bool operator!=(const rect_s& rhs) const noexcept {
            return ((rhs.m_x != m_x) || (rhs.m_y != m_y) || (rhs.m_width != m_width) || (rhs.m_height != m_height));
        }
    } rect_t;

    //------------------------------------------------------------------------------
    // Display
    //------------------------------------------------------------------------------

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

            if ((m_nv_num_physical_gpus != 1) || (m_nv_mosaic_num_displays > 1)) {
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
        
        size_t refresh_rate() const noexcept { return m_refresh_rate; }
        void set_refresh_rate(size_t refresh_rate) { m_refresh_rate = refresh_rate; }

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
        
        size_t                  m_refresh_rate = 0;

        NvU32                   m_nv_display_id = 0;
        NvDisplayHandle         m_nv_display_handle = nullptr;
        size_t                  m_nv_num_physical_gpus = 0;

        size_t                  m_nv_mosaic_num_displays = 0;
    };

    //------------------------------------------------------------------------------
    // DisplayConfiguration
    //------------------------------------------------------------------------------

    class DisplayConfiguration
    {
    public:

        DisplayConfiguration()
        {
            //------------------------------------------------------------------------------
            // Get display info.
            enum_displays();
            enum_mosaics();
            const rect_t vr_virtual_screen_rect = identify_openvr_display();

            //------------------------------------------------------------------------------
            // Select displays.
            size_t max_num_displays = 1;

            for (const auto& display : m_displays) {
                if (display->valid_non_mosaic()) {
                    if (display->virtual_screen_rect() == vr_virtual_screen_rect) {
                        m_openvr_display = display;
                    }
                    else if (!m_control_display) {
                        m_control_display = display;
                    }
                }
                else if (display->valid_mosaic() && (display->nv_mosaic_num_displays() > max_num_displays)) {
                    m_mosaic_display = display;
                    max_num_displays = display->nv_mosaic_num_displays();
                }
                else {
                    throw std::runtime_error("Invalid display!");
                }
            }

            if (!m_control_display && m_openvr_display && m_is_openvr_display_in_direct_mode) {
                m_control_display = m_openvr_display;
            }

            if (!m_control_display) {
                throw std::runtime_error("No valid control display available!");
            }

            if (!m_mosaic_display && !m_openvr_display) {
                throw std::runtime_error("No valid Mosaic/OpenVR display available!");
            }
        }

     public:

         std::shared_ptr<Display> control_display() const noexcept { return m_control_display; }
         std::shared_ptr<Display> mosaic_display() const noexcept { return m_mosaic_display; }
         std::shared_ptr<Display> openvr_display() const noexcept { return m_openvr_display; }
         bool openvr_display_in_direct_mode() const noexcept { return m_is_openvr_display_in_direct_mode; }

    private:

        std::vector<std::shared_ptr<Display>>       m_displays;

        std::shared_ptr<Display>                    m_primary_display;
        std::shared_ptr<Display>                    m_control_display;
        std::shared_ptr<Display>                    m_mosaic_display;
        std::shared_ptr<Display>                    m_openvr_display;
        bool                                        m_is_openvr_display_in_direct_mode = false;

        //------------------------------------------------------------------------------
        // Get a list of physical displays (monitors) from Windows.
        void enum_displays()
        {
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
                const rect_t virtual_screen_rect = {
                    virtual_screen_rect_->left,
                    virtual_screen_rect_->top,
                    (virtual_screen_rect_->right - virtual_screen_rect_->left),
                    (virtual_screen_rect_->bottom - virtual_screen_rect_->top)
                };

                //------------------------------------------------------------------------------
                // Get additional monitor info.
                MONITORINFOEX monitor_info = {};
                monitor_info.cbSize = sizeof(monitor_info);

                std::shared_ptr<Display> display;
                bool is_primary = false;

                if (GetMonitorInfo(monitor, &monitor_info) != 0) {
                    std::cout << "Monitor " << monitor_info.szDevice << ": ";

                    display.reset(new Display(monitor_info.szDevice, virtual_screen_rect));
                    is_primary = ((monitor_info.dwFlags & MONITORINFOF_PRIMARY) == MONITORINFOF_PRIMARY);
                }
                else {
                    std::cout << "Monitor 0x" << monitor << ": ";
                }

                std::cout << "(" << virtual_screen_rect.m_x << " / " << virtual_screen_rect.m_y << ") [" << virtual_screen_rect.m_width << " x " << virtual_screen_rect.m_height << "]";

                if (is_primary) {
                    std::cout << " (primary)";
                }

                if (display) {
                    if (is_primary) {
                        assert(!this_->m_primary_display);
                        this_->m_primary_display = display;
                    }

                    this_->m_displays.emplace_back(std::move(display));
                }

                std::cout << std::endl;

                //------------------------------------------------------------------------------
                // ...
                return TRUE;
            }, LPARAM(this)) == 0)
            {
                throw std::runtime_error("Failed to enumerate monitors!");
            }

            if (!m_primary_display) {
                throw std::runtime_error("Failed to identify primary display!");
            }
        }

        //------------------------------------------------------------------------------
        // Get Mosaic information for displays.
        void enum_mosaics()
        {
            assert(m_primary_display);  // Call enum_displays() first!

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
                    std::cout << "Warning: Mosaic is DISABLED (but possible)!" << std::endl;
                    return;
                }
                else {
                    std::cout << "Warning: Mosaic is DISABLED!" << std::endl;
                    return;
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
                    (*it)->set_refresh_rate(display_grid.displaySettings.freq);
                    (*it)->set_nv_mosaic_num_displays(display_grid.displayCount);
                }

                //------------------------------------------------------------------------------
                // Print display grid info to console.
                std::cout << "Display Grid " << display_grid_index++ << std::endl;
                print_to_stream(std::cout, display_grid, "  ");
            }
        }

        //------------------------------------------------------------------------------
        // Detect OpenVR HMD.
        rect_t identify_openvr_display()
        {
            assert(m_primary_display);  // Call enum_displays() first!

            rect_t virtual_screen_rect;

            if (!vr::VR_IsHmdPresent()) {
                return virtual_screen_rect;
            }

            vr::IVRSystem* const vr_system = vr::VRSystem();

            if (!vr_system) {
                return virtual_screen_rect;
            }

            uint64_t vr_device_luid = 0;
            vr_system->GetOutputDevice(&vr_device_luid, vr::TextureType_OpenGL);
            std::cout << "OpenVR output device (LUID): 0x" << std::hex << std::setfill('0') << std::setw(8) << vr_device_luid << std::dec << std::endl;

            if (vr_system->IsDisplayOnDesktop()) {
                std::cout << "OpenVR is in extended mode" << std::endl;

                vr::IVRExtendedDisplay* const vr_extended_display = vr::VRExtendedDisplay();

                if (vr_extended_display) {
                    int32_t x = 0;
                    int32_t y = 0;
                    uint32_t w = 0;
                    uint32_t h = 0;

                    vr_extended_display->GetWindowBounds(&x, &y, &w, &h);
                    std::cout << "OpenVR window bounds: " << x << ", " << y << ", " << w << ", " << h << std::endl;

                    virtual_screen_rect.m_x = x;
                    virtual_screen_rect.m_y = y;
                    virtual_screen_rect.m_width = w;
                    virtual_screen_rect.m_height = h;

                    for (size_t i = 0; i < 2; ++i) {
                        uint32_t x = 0;
                        uint32_t y = 0;

                        vr_extended_display->GetEyeOutputViewport(vr::EVREye(i), &x, &y, &w, &h);
                        std::cout << "OpenVR " << (i == vr::Eye_Left ? "left" : "right") << " eye viewport: " << x << ", " << y << ", " << w << ", " << h << std::endl;
                    }
                }
            }
            else {
                //------------------------------------------------------------------------------
                // OpenVR demands that the HMD is attached to the same GPU as the primary
                // display (if this is not the case the SteamVR will notify the user). Set the
                // direct mode flag to indicate that the OpenVR display identifies the primary
                // display as opposed to the extended display scanned out to the HMD.
                virtual_screen_rect = m_primary_display->virtual_screen_rect();
                m_is_openvr_display_in_direct_mode = true;

#ifndef NDEBUG
                verify_primary_display_connected_to_device(vr_device_luid, virtual_screen_rect);
#endif // NDEBUG
            }

            return virtual_screen_rect;
        }

        //------------------------------------------------------------------------------
        // DirectX appears to be the shortest link between LUID and display name so we
        // use it to verify the primary display is indeed attached to the same GPU as
        // the OpenVR HMD.
        void verify_primary_display_connected_to_device(uint64_t vr_device_luid, const rect_t& primary_display_virtual_screen_rect)
        {
            bool primary_display_connected_to_device = false;

            //------------------------------------------------------------------------------
            // Grab DirectX factory.
            Microsoft::WRL::ComPtr<IDXGIFactory> factory;

            if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&factory)))) {
                throw std::runtime_error("Failed to create DXGI factory!");
            }

            //------------------------------------------------------------------------------
            // Find DirectX adapter (GPU) identified by OpenVR.
            LUID device_luid = {};
            {
                device_luid.LowPart = DWORD(vr_device_luid);
                device_luid.HighPart = LONG(vr_device_luid >> (sizeof(device_luid.LowPart) * 8));
            }

            UINT adapter_index = 0;
            IDXGIAdapter* adapter = nullptr;

            while (factory->EnumAdapters(adapter_index, &adapter) != DXGI_ERROR_NOT_FOUND) {
                DXGI_ADAPTER_DESC adapter_desc = {};

                if (FAILED(adapter->GetDesc(&adapter_desc))) {
                    std::cerr << "Error: Failed to get adapter description!" << std::endl;
                    continue;
                }

                std::cout << "Adapter " << adapter_index << ": ";
                std::wcout << adapter_desc.Description;
                std::cout << ", 0x" << std::hex << adapter_desc.AdapterLuid.HighPart << std::setfill('0') << std::setw(8) << adapter_desc.AdapterLuid.LowPart << std::dec;
                std::cout << std::endl;

                const bool is_vr_device_adapter = (adapter_desc.AdapterLuid.LowPart == device_luid.LowPart) && (adapter_desc.AdapterLuid.HighPart == device_luid.HighPart);

                if (is_vr_device_adapter) {
                    //------------------------------------------------------------------------------
                    // Enumerate outputs (displays).
                    UINT output_index = 0;
                    IDXGIOutput* output = nullptr;

                    while (adapter->EnumOutputs(output_index, &output) != DXGI_ERROR_NOT_FOUND) {
                        DXGI_OUTPUT_DESC output_desc = {};

                        if (FAILED(output->GetDesc(&output_desc))) {
                            std::cerr << "Error: Failed to get output description!" << std::endl;
                            continue;
                        }

                        std::cout << "  Output " << output_index << ": ";
                        std::wcout << output_desc.DeviceName;

                        bool first = true;

                        if (output_desc.AttachedToDesktop) {
                            if (first) { std::cout << " ("; }
                            else { std::cout << ", "; }
                            std::cout << "display attached";
                            first = false;
                        }

                        MONITORINFO monitor_info = {};
                        monitor_info.cbSize = sizeof(monitor_info);

                        if (GetMonitorInfo(output_desc.Monitor, &monitor_info)) {
                            if (monitor_info.dwFlags & MONITORINFOF_PRIMARY) {
                                if (first) { std::cout << " ("; }
                                else { std::cout << ", "; }
                                std::cout << "primary display";
                                first = false;

                                const rect_t virtual_screen_rect(
                                    monitor_info.rcMonitor.left,
                                    monitor_info.rcMonitor.top,
                                    (monitor_info.rcMonitor.right - monitor_info.rcMonitor.left),
                                    (monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top));

                                if (virtual_screen_rect != primary_display_virtual_screen_rect) {
                                    throw std::runtime_error("Expected primary display virtual screen rectangles to match!");
                                }

                                primary_display_connected_to_device = true;
                            }
                        }

                        if (!first) { std::cout << ")"; }
                        std::cout << std::endl;

                        output->Release();
                        ++output_index;

                        if (primary_display_connected_to_device) {
                            break;
                        }
                    }
                }

                adapter->Release();
                ++adapter_index;

                if (is_vr_device_adapter) {
                    break;
                }
            }

            if (!primary_display_connected_to_device) {
                throw std::runtime_error("Primary display is not connected to the given GPU (LUID)!");
            }
        }
    };

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

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
        const long inset = 100;

        GLFWwindow* const control_window = glfwCreateWindow((control_display->virtual_screen_rect().m_width - (inset * 2)), (control_display->virtual_screen_rect().m_height - (inset * 2)), "VMI Player", NULL, NULL);

        if (!control_window) {
            throw std::runtime_error("Failed to create main window!");
        }

        glfwSetKeyCallback(control_window, &control_window_key_callback);
        glfwSetWindowPos(control_window, (control_display->virtual_screen_rect().m_x + inset), (control_display->virtual_screen_rect().m_y + inset));
        glfwShowWindow(control_window);

        //------------------------------------------------------------------------------
        // ...
        return control_window;
    }

    LRESULT CALLBACK mosaic_window_callback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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
            wc.lpszClassName = "VMI Player";
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

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    HGLRC create_opengl_affinity_context(HDC& affinity_display_context, HGPUNV gpu, const PIXELFORMATDESCRIPTOR& pixel_format_desc)
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
            WGL_CONTEXT_MAJOR_VERSION_ARB, GL_CONTEXT_VERSION_MAJOR,
            WGL_CONTEXT_MINOR_VERSION_ARB, GL_CONTEXT_VERSION_MINOR,
            WGL_CONTEXT_FLAGS_ARB, (GL_OPENGL_DEBUG_CONTEXT == GLFW_TRUE ? WGL_CONTEXT_DEBUG_BIT_ARB : 0),
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

    //------------------------------------------------------------------------------
    // No specific pixel format is required for an affinity (display) context as it
    // does not have a default framebuffer, however we do bind to the mosaic window
    // display context for the final pass and thus must match its pixel format.
    void create_render_contexts(std::shared_ptr<Display> stereo_display, const PIXELFORMATDESCRIPTOR& pixel_format_desc)
    {
        //------------------------------------------------------------------------------
        // Identify primary/support GPUs.
        std::deque<HGPUNV> unassigned_gpus;
        HGPUNV primary_gpu = nullptr;

        UINT gpu_index = 0;
        HGPUNV gpu = nullptr;

        while (wglEnumGpusNV(gpu_index, &gpu)) {
            std::cout << "GPU " << gpu_index << ":" << std::endl;

            //------------------------------------------------------------------------------
            // Enumerate devices (displays).
            GPU_DEVICE gpu_device;
            gpu_device.cb = sizeof(gpu_device);

            bool is_primary_gpu = false;

            for (UINT device_index = 0; wglEnumGpuDevicesNV(gpu, device_index, &gpu_device); ++device_index) {
                std::cout << "  Device " << device_index << ": ";
                std::cout << gpu_device.DeviceString << ", " << gpu_device.DeviceName << ", ";
                print_display_flags_to_stream(std::cout, gpu_device.Flags);
                std::cout << std::endl;

                if (stereo_display && (stereo_display->name() == gpu_device.DeviceName)) {
                    is_primary_gpu = true;
                }
            }

            if (is_primary_gpu) {
                assert(!primary_gpu);
                primary_gpu = gpu;
            }
            else {
                unassigned_gpus.push_back(gpu);
            }

            ++gpu_index;
        }

        if (!primary_gpu) {
            if (stereo_display) {
                throw std::runtime_error("Failed to identify the primary GPU!");
            }

            if (unassigned_gpus.empty()) {
                throw std::runtime_error("Failed to identify a primary GPU!");
            }

            primary_gpu = unassigned_gpus.front();
            unassigned_gpus.pop_front();
        }

        if (unassigned_gpus.empty()) {
            throw std::runtime_error("Failed to identify a support GPU!");
        }

        const HGPUNV support_gpu = unassigned_gpus.front();
        unassigned_gpus.pop_front();

        //------------------------------------------------------------------------------
        // Create the OpenGL affinity contexts.
        assert(primary_gpu != support_gpu);

        primary_gl_context = create_opengl_affinity_context(primary_dc, primary_gpu, pixel_format_desc);
        support_gl_context = create_opengl_affinity_context(support_dc, support_gpu, pixel_format_desc);
    }

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

    void create_texture_backed_render_targets(
        GLuint* const framebuffers,
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

    void delete_texture_backed_render_targets(
        const GLuint* const framebuffers,
        const GLuint* const color_attachments,
        size_t n)
    {
        glDeleteTextures(GLsizei(n), color_attachments);
        glDeleteFramebuffers(GLsizei(n), framebuffers);
    }

    ////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    bool ALWAYS_USE_OPENVR_HMD_WHEN_AVAILABLE = true;

    //------------------------------------------------------------------------------
    // Initialize NVAPI.
    if (NvAPI_Initialize() != NVAPI_OK) {
        throw std::runtime_error("Failed to initialize NVAPI!");
    }

    atexit([]() {
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
    glfwSetErrorCallback([](int error, const char* const description) {
        std::cerr << "GLFW: " << error << ": " << description << std::endl;
    });

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
    // Initialize OpenVR if available.
    vr::IVRCompositor* vr_compositor = nullptr;

    if (vr::VR_IsRuntimeInstalled()) {
        vr::EVRInitError error = vr::VRInitError_None;
        vr::IVRSystem* vr_system = vr::VR_Init(&error, vr::VRApplication_Scene);

        if ((error != vr::VRInitError_None) || !vr_system) {
            std::cerr << "Error: Failed to initialize VR system: " << error << ": " << vr::VR_GetVRInitErrorAsEnglishDescription(error) << std::endl;
            return EXIT_FAILURE;
        }

        atexit([]() {
            vr::VR_Shutdown();
        });

        //------------------------------------------------------------------------------
        // Keep log a bit cleaner by giving the VR sytstem a chance to complete its
        // asynchronous startup before we go on.
        std::this_thread::sleep_for(std::chrono::seconds(2));

        //------------------------------------------------------------------------------
        // Grab compositor for later use.
        vr_compositor = vr::VRCompositor();
    }

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

        glewExperimental = GL_TRUE;
        const GLenum glew_result = glewInit();

        if (glew_result != GLEW_OK) {
            std::cerr << "Error: Failed to initialize GLEW: " << glewGetErrorString(glew_result) << std::endl;
            return EXIT_FAILURE;
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
            // alpha bitplanes." [PIXELFORMATDESCRIPTOR documentation]
            pixel_format_desc.iPixelType = PFD_TYPE_RGBA;
            pixel_format_desc.cColorBits = 24;
        };

        std::shared_ptr<Display> stereo_display;
        HWND stereo_display_window = nullptr;

        if (display_configuration.openvr_display() && (ALWAYS_USE_OPENVR_HMD_WHEN_AVAILABLE || !display_configuration.mosaic_display())) {
            stereo_display = display_configuration.openvr_display();

            if (!display_configuration.openvr_display_in_direct_mode()) {
                stereo_display_window = create_stereo_display_window(stereo_display, pixel_format_desc);

                //------------------------------------------------------------------------------
                // If the HMD is not in direct mode move the OpenVR compositor window out of the
                // way as we are not rendering to it but use our own 'fullscreen' window.
                if (vr_compositor) {
                    vr_compositor->CompositorGoToBack();
                }
            }
        }
        else {
            stereo_display = display_configuration.mosaic_display();
            stereo_display_window = create_stereo_display_window(stereo_display, pixel_format_desc);
        }

        create_render_contexts(stereo_display, pixel_format_desc);

        //------------------------------------------------------------------------------
        // Create OpenGL objects.
        GLuint stereo_framebuffers[2] = {};
        GLuint stereo_textures[2] = {};

        if (primary_dc && primary_gl_context) {
            if (!wglMakeCurrent(primary_dc, primary_gl_context)) {
                return EXIT_FAILURE;
            }

            create_texture_backed_render_targets(stereo_framebuffers, stereo_textures, 2, 1440, 1600);
        }


        if (support_dc && support_gl_context) {
            if (!wglMakeCurrent(support_dc, support_gl_context)) {
                return EXIT_FAILURE;
            }
        }

        //------------------------------------------------------------------------------
        // Run loop.
        double time = 0.0;

        while (!glfwWindowShouldClose(control_window)) {
            //------------------------------------------------------------------------------
            // Render left/right eye.
            if ((1)) {
                wglMakeCurrent(support_dc, support_gl_context);
                wglMakeCurrent(primary_dc, primary_gl_context);
            }

            //------------------------------------------------------------------------------
            // Combine into stereo display window.
            if (stereo_display_window) {
                const HDC stereo_display_dc = GetDC(stereo_display_window);

                if (wglMakeCurrent(stereo_display_dc, primary_gl_context)) {
                    glClearColor(0.25, 0.5, GLclampf(time), 1.0);
                    glClear(GL_COLOR_BUFFER_BIT);

                    SwapBuffers(stereo_display_dc);
                }

                ReleaseDC(stereo_display_window, stereo_display_dc);
            }
            else {
                assert(display_configuration.openvr_display_in_direct_mode() && vr_compositor);

                wglMakeCurrent(primary_dc, primary_gl_context);

                glBindFramebuffer(GL_FRAMEBUFFER, stereo_framebuffers[0]);
                glClearColor(0.5, 0.25, GLclampf(time), 1.0);
                glClear(GL_COLOR_BUFFER_BIT);

                glBindFramebuffer(GL_FRAMEBUFFER, stereo_framebuffers[1]);
                glClearColor(0.25, 0.5, GLclampf(time), 1.0);
                glClear(GL_COLOR_BUFFER_BIT);

                vr::Texture_t eye_texture = {};
                {
                    eye_texture.eType = vr::TextureType_OpenGL;
                    eye_texture.eColorSpace = vr::ColorSpace_Linear;
                }

                vr::VRTextureBounds_t bounds = {};
                {
                    bounds.uMin = 0.0;
                    bounds.vMin = 0.0;
                    bounds.uMax = 1.0;
                    bounds.vMax = 1.0;
                }

                vr_compositor->WaitGetPoses(nullptr, 0, nullptr, 0);

                for (size_t eye_index = 0; eye_index < 2; ++eye_index) {
                    eye_texture.handle = (void*)uintptr_t(stereo_textures[eye_index]);
                    
                    const vr::EVRCompositorError error = vr_compositor->Submit(vr::EVREye(eye_index), &eye_texture, &bounds, vr::Submit_Default);

                    if (error != vr::VRCompositorError_None) {
                        std::cerr << "Error: " << error << std::endl;
                    }
                }

                glFlush();
            }

            //------------------------------------------------------------------------------
            // Render control window.
            glfwMakeContextCurrent(control_window);
            glClearColor(0.5, 0.25, GLclampf(time), 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
            
            glfwSwapBuffers(control_window);

            //------------------------------------------------------------------------------
            // Handle events for all windows.
            glfwPollEvents();

            time += (1.0 / 30.0);
            time = fmod(time, 1.0);
        }
    }
    catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
