//
//  DisplayConfiguration.cpp
//  vmi-player
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DisplayConfiguration.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <deque>
#include <iostream>
#include <set>

#include "_OpenGLApi.h"
#include "_OpenVRApi.h"

#include <dxgi1_6.h>
#include <wrl.h>

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

    constexpr bool OPENGL_DEBUG_CONTEXT = (IS_DEBUG_BUILD ? true : false);

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

                stream << indent << "[" << r << "," << c << "] " << toolbox::StlUtils::hex_insert(display_id, 8) << std::endl;
            }
        }
    }

    void print_display_flags_to_stream(std::ostream& stream, DWORD flags)
    {
        stream << toolbox::StlUtils::hex_insert(flags);

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

    HGLRC create_opengl_affinity_context(
        HDC& affinity_display_context,
        HGPUNV gpu,
        const PIXELFORMATDESCRIPTOR& pixel_format_desc,
        int context_version_major,
        int context_version_minor)
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
        // Create OpenGL affinity context.
        const int attrib_list[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, context_version_major,
            WGL_CONTEXT_MINOR_VERSION_ARB, context_version_minor,
            WGL_CONTEXT_FLAGS_ARB, (OPENGL_DEBUG_CONTEXT ? WGL_CONTEXT_DEBUG_BIT_ARB : 0),
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

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DisplayConfiguration::DisplayConfiguration()
{
    //------------------------------------------------------------------------------
    // Get display info.
    enum_displays();
    enum_logical_gpus();

    try {
        enum_mosaics();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Failed to enumerate mosaics: Unknown exception!" << std::endl;
    }

    glm::uvec2 vr_render_resolution = glm::uvec2(0, 0);
    const rect_t vr_virtual_screen_rect = identify_openvr_display(vr_render_resolution);

    //------------------------------------------------------------------------------
    // Select displays.
    std::set<std::shared_ptr<Display>> assigned_displays;

    for (const auto& display : m_displays) {
        const bool is_mosaic = display->valid_mosaic();

        if (is_mosaic) {
            if (!m_mosaic_display || (display->nv_mosaic_num_displays() > m_mosaic_display->nv_mosaic_num_displays())) {
                m_mosaic_display = display;
                assigned_displays.insert(display);
            }
        }
        //------------------------------------------------------------------------------
        //! OpenVR does not always return correct y coordinate for virtual screen rect.
        //! We ignore x also and go by width and height assuming its a unique display
        //! resolution only used by HMDs.
        else if ((display->virtual_screen_rect().m_width == vr_virtual_screen_rect.m_width) && (display->virtual_screen_rect().m_height == vr_virtual_screen_rect.m_height)) {
            assert(!m_openvr_display);

            if (m_is_openvr_display_in_direct_mode) {
                m_openvr_display.reset(new Display("OPENVR", (*display)));
                m_displays.emplace_back(m_openvr_display);
            }
            else {
                m_openvr_display = display;
            }

            m_openvr_display->set_render_resolution(vr_render_resolution);
            assigned_displays.insert(m_openvr_display);
        }
    }

    if (IS_DEBUG_BUILD && !m_mosaic_display) {
        m_mosaic_display = m_primary_display;
    }

    if (!m_mosaic_display && !m_openvr_display) {
        throw std::runtime_error("Expected a valid Mosaic or OpenVR display!");
    }

    if (m_mosaic_display) {
        std::cout << "Mosaic display: " << (*m_mosaic_display) << std::endl;
    }

    if (m_openvr_display) {
        std::cout << "OpenVR display: " << (*m_openvr_display) << std::endl;
    }

    for (const auto& display : m_displays) {
        if ((!m_mosaic_display || (display->logical_gpu_index() != m_mosaic_display->logical_gpu_index())) &&
            (!m_openvr_display || (display->logical_gpu_index() != m_openvr_display->logical_gpu_index())))
        {
            m_control_display = display;
            assigned_displays.insert(display);
            break;
        }
    }

    if (!m_control_display) {
        for (const auto& display : m_displays) {
            if ((display != m_mosaic_display) && (display != m_openvr_display)) {
                m_control_display = display;
                assigned_displays.insert(display);
                break;
            }
        }
    }

    if (IS_DEBUG_BUILD && !m_control_display) {
        m_control_display = m_primary_display;
        assigned_displays.insert(m_primary_display);
    }

    if (!m_control_display) {
        throw std::runtime_error("Expected a valid control display!");
    }

    std::cout << "Control display: " << (*m_control_display) << std::endl;

#ifndef NDEBUG
    for (const auto& display : m_displays) {
        if (assigned_displays.find(display) == end(assigned_displays)) {
            std::cout << "Unassigned display: " << (*display) << std::endl;
        }
    }
#endif // NDEBUG

    if (m_mosaic_display && (m_control_display->logical_gpu_index() == m_mosaic_display->logical_gpu_index())) {
        std::cout << "Warning: Control display is on same GPU as the Mosaic display!" << std::endl;
    }

    if (m_openvr_display && (m_control_display->logical_gpu_index() == m_openvr_display->logical_gpu_index())) {
        std::cout << "Warning: Control display is on same GPU as the OpenVR display!" << std::endl;
    }
}

void DisplayConfiguration::create_render_contexts(
    std::pair<HDC, HGLRC>& primary_context,
    std::pair<HDC, HGLRC>& support_context,
    std::shared_ptr<Display> stereo_display,
    const PIXELFORMATDESCRIPTOR& pixel_format_desc,
    int context_version_major,
    int context_version_minor
)
{
    //------------------------------------------------------------------------------
    // Identify primary/support GPUs.
    std::deque<std::pair<HGPUNV, size_t>> unassigned_gpus;
    HGPUNV primary_gpu = nullptr;
    size_t primary_gpu_index = size_t(-1);

    UINT gpu_index = 0;
    HGPUNV gpu = nullptr;

    while (wglEnumGpusNV(gpu_index, &gpu)) {
        std::cout << "OpenGL GPU " << gpu_index << ":" << std::endl;

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

            if (stereo_display && (stereo_display->gpu_association_name() == gpu_device.DeviceName)) {
                is_primary_gpu = true;
            }
        }

        if (is_primary_gpu) {
            assert(!primary_gpu);
            primary_gpu = gpu;
            primary_gpu_index = gpu_index;
        }
        else {
            unassigned_gpus.emplace_back(gpu, gpu_index);
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

        primary_gpu = unassigned_gpus.front().first;
        primary_gpu_index = unassigned_gpus.front().second;
        unassigned_gpus.pop_front();
    }

    if (unassigned_gpus.empty()) {
        throw std::runtime_error("Failed to identify a support GPU!");
    }

    const HGPUNV support_gpu = unassigned_gpus.front().first;
    const size_t support_gpu_index = unassigned_gpus.front().second;
    unassigned_gpus.pop_front();

    std::cout << "Primary OpenGL GPU: " << primary_gpu_index << std::endl;
    std::cout << "Support OpenGL GPU: " << support_gpu_index << std::endl;

    //------------------------------------------------------------------------------
    // Create the OpenGL affinity contexts.
    assert(primary_gpu != support_gpu);
    assert(primary_gpu_index != support_gpu_index);

    primary_context.second = create_opengl_affinity_context(
        primary_context.first,
        primary_gpu,
        pixel_format_desc,
        context_version_major,
        context_version_minor
        );

    support_context.second = create_opengl_affinity_context(
        support_context.first,
        primary_gpu,
        pixel_format_desc,
        context_version_major,
        context_version_minor
    );
}

void DisplayConfiguration::enum_displays()
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
            std::cout << "Monitor 0x" << toolbox::StlUtils::hex_insert(monitor);
        }

        std::cout << "(" << virtual_screen_rect.m_x << " / " << virtual_screen_rect.m_y << ") [" << virtual_screen_rect.m_width << " x " << virtual_screen_rect.m_height << "]";

        if (is_primary) {
            std::cout << " (primary display)";
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

void DisplayConfiguration::enum_logical_gpus()
{
    //------------------------------------------------------------------------------
     // Grab DirectX factory.
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;

    if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&factory)))) {
        throw std::runtime_error("Failed to create DXGI factory!");
    }

    //------------------------------------------------------------------------------
    // Enumerate DirectX adapters (logical GPUs).
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

        const uint64_t luid = ((uint64_t(adapter_desc.AdapterLuid.HighPart) << (sizeof(adapter_desc.AdapterLuid.LowPart) * 8)) | adapter_desc.AdapterLuid.LowPart);
        std::cout << ", " << toolbox::StlUtils::hex_insert(luid) << std::endl;

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
            std::cout << std::endl;

            char display_name[sizeof(output_desc.DeviceName) * 4];
            size_t display_name_length = 0;

            wcstombs_s(&display_name_length, display_name, sizeof(display_name), output_desc.DeviceName, _TRUNCATE);

            const auto display_it = std::find_if(begin(m_displays), end(m_displays), [display_name](const std::shared_ptr<Display>& display) {
                return (display->gpu_association_name() == display_name);
            });

            if (display_it == end(m_displays)) {
                std::cout << "  Warning: DirectX enumerates display " << display_name << " but Windows does not!" << std::endl;
            }
            else {
                if ((*display_it)->logical_gpu_index() == Display::INVALID_LOGICAL_GPU_INDEX) {
                    (*display_it)->set_logical_gpu_index(adapter_index, Display::LogicalGPUIndexSource::DIRECTX);
                }
            }

            output->Release();
            ++output_index;
        }

        adapter->Release();
        ++adapter_index;
    }
}

void DisplayConfiguration::enum_mosaics()
{
    assert(m_primary_display);  // Call enum_displays() first!

    //------------------------------------------------------------------------------
    // Enumerate displays and note NVIDIA display IDs which are required below.
    NvLogicalGpuHandle logical_gpu_handles[NVAPI_MAX_LOGICAL_GPUS];
    NvU32 num_logical_gpus = 0;

    if (NvAPI_EnumLogicalGPUs(logical_gpu_handles, &num_logical_gpus) != NVAPI_OK) {
        throw std::runtime_error("Failed to enumerate logical GPUs!");
    }

    NvDisplayHandle display_handle = nullptr;

    for (NvU32 display_index = 0; NvAPI_EnumNvidiaDisplayHandle(display_index, &display_handle) == NVAPI_OK; ++display_index) {
        NvAPI_ShortString display_name = {};

        if (NvAPI_GetAssociatedNvidiaDisplayName(display_handle, display_name) != NVAPI_OK) {
            std::cout << "Warning: NVAPI enumerates nameless display " << toolbox::StlUtils::hex_insert(display_handle) << "!" << std::endl;
            continue;
        }

        const auto display_it = std::find_if(begin(m_displays), end(m_displays), [display_name](const std::shared_ptr<Display>& display) {
            return (display->gpu_association_name() == display_name);
        });

        if (display_it == end(m_displays)) {
            std::cout << "Warning: NVAPI enumerates display " << display_name << " but Windows does not!" << std::endl;
            continue;
        }

        NvU32 display_id = 0;

        if (NvAPI_DISP_GetDisplayIdByDisplayName(display_name, &display_id) == NVAPI_OK) {
            NvPhysicalGpuHandle physical_gpus[NVAPI_MAX_PHYSICAL_GPUS] = {};
            NvU32 num_physical_gpus = 0;

            if (NvAPI_GetPhysicalGPUsFromDisplay(display_handle, physical_gpus, &num_physical_gpus) != NVAPI_OK) {
                throw std::runtime_error("Failed to get physical GPU count!");
            }

            (*display_it)->set_nv_display(display_id, display_handle, num_physical_gpus);

            if ((*display_it)->logical_gpu_index() == Display::INVALID_LOGICAL_GPU_INDEX) {
                NvLogicalGpuHandle logical_gpu_handle = 0;

                if (NvAPI_GetLogicalGPUFromDisplay(display_handle, &logical_gpu_handle) != NVAPI_OK) {
                    throw std::runtime_error("Failed to get logical GPU handle!");
                }

                const auto logical_gpu_handle_it = std::find(std::begin(logical_gpu_handles), std::end(logical_gpu_handles), logical_gpu_handle);

                if (logical_gpu_handle_it == std::end(logical_gpu_handles)) {
                    throw std::runtime_error("Failed to find logical GPU index!");
                }

                const size_t logical_gpu_index = std::distance(std::begin(logical_gpu_handles), logical_gpu_handle_it);
                (*display_it)->set_logical_gpu_index(logical_gpu_index, Display::LogicalGPUIndexSource::NVAPI);
            }
        }
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
        //------------------------------------------------------------------------------
        // Print display grid info to console.
        std::cout << "Display Grid " << display_grid_index++ << std::endl;
        print_to_stream(std::cout, display_grid, "  ");

        for (size_t display_index = 0; display_index < display_grid.displayCount; ++display_index) {
            const NvU32 display_id = display_grid.displays[display_index].displayId;

            const auto it = std::find_if(begin(m_displays), end(m_displays), [display_id](const std::shared_ptr<Display>& display) {
                return (display->nv_display_id() == display_id);
            });

            if (it == end(m_displays)) {
                std::cout << "  Warning: NVAPI enumerates display " << toolbox::StlUtils::hex_insert(display_id) << " but Windows does not!" << std::endl;
            }
            else {
                (*it)->set_refresh_rate(display_grid.displaySettings.freq);
                (*it)->set_nv_mosaic_num_displays(display_grid.displayCount);
                break;
            }
        }
    }
}

rect_t DisplayConfiguration::identify_openvr_display(glm::uvec2& render_resolution)
{
    assert(m_primary_display);  // Call enum_displays() first!

    rect_t virtual_screen_rect;

    vr::IVRSystem* const vr_system = vr::VRSystem();

    if (!vr_system) {
        return virtual_screen_rect;
    }

    uint64_t vr_device_luid = 0;
    vr_system->GetOutputDevice(&vr_device_luid, vr::TextureType_OpenGL);
    std::cout << "OpenVR output device (LUID): " << toolbox::StlUtils::hex_insert(vr_device_luid) << std::endl;

    if (vr_system->IsDisplayOnDesktop()) {
        std::cout << "OpenVR is in extended mode" << std::endl;

        vr::IVRExtendedDisplay* const vr_extended_display = vr::VRExtendedDisplay();

        if (vr_extended_display) {
            int32_t x = 0;
            int32_t y = 0;
            uint32_t w = 0;
            uint32_t h = 0;

            vr_extended_display->GetWindowBounds(&x, &y, &w, &h);
            std::cout << "OpenVR window bounds: (" << x << " / " << y << ") [" << w << " x " << h << "]" << std::endl;

            virtual_screen_rect = rect_t(x, y, w, h);

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

    {
        uint32_t width = 0;
        uint32_t height = 0;

        vr_system->GetRecommendedRenderTargetSize(&width, &height);
        render_resolution = glm::uvec2((width * 2), height);
    }

    return virtual_screen_rect;
}

void DisplayConfiguration::verify_primary_display_connected_to_device(uint64_t vr_device_luid, const rect_t& primary_display_virtual_screen_rect)
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

        const uint64_t luid = ((uint64_t(adapter_desc.AdapterLuid.HighPart) << (sizeof(adapter_desc.AdapterLuid.LowPart) * 8)) | adapter_desc.AdapterLuid.LowPart);
        std::cout << ", " << toolbox::StlUtils::hex_insert(luid) << std::endl;

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
