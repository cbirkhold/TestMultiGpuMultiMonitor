//
//  DisplayConfiguration.h
//  vmi-player
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __DISPLAY_CONFIGURATION_H__
#define __DISPLAY_CONFIGURATION_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <vector>

#include "__WindowsApi.h"

#include "_GLMApi.h"
#include "_NVApi.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "CppUtils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Display
//------------------------------------------------------------------------------

class Display
{
public:

    static constexpr size_t INVALID_LOGICAL_GPU_INDEX = size_t(-1);

    enum class LogicalGPUIndexSource {
        DIRECTX,
        NVAPI,
    };

public:

    Display(std::string name, const Display& from)
        : m_name(std::move(name))
        , m_gpu_association_name(from.m_gpu_association_name)
        , m_virtual_screen_rect(from.m_virtual_screen_rect)
        , m_render_resolution(from.m_render_resolution)
        , m_refresh_rate(from.m_refresh_rate)
        , m_logical_gpu_index(from.m_logical_gpu_index)
    {
        if (m_name.empty()) {
            throw std::runtime_error("Valid name expected!");
        }
    }

    Display(std::string name, const rect_t& virtual_screen_rect)
        : m_name(std::move(name))
        , m_gpu_association_name(m_name)
        , m_virtual_screen_rect(virtual_screen_rect)
        , m_render_resolution(virtual_screen_rect.m_width, virtual_screen_rect.m_height)
    {
        if (m_name.empty()) {
            throw std::runtime_error("Valid name expected!");
        }

        if ((m_virtual_screen_rect.m_width == 0) || (m_virtual_screen_rect.m_height == 0)) {
            throw std::runtime_error("Valid virtual screen rect expected!");
        }
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
    const std::string& gpu_association_name() const noexcept { return m_gpu_association_name; }

    const rect_t& virtual_screen_rect() const noexcept { return m_virtual_screen_rect; }

    glm::uvec2 render_resolution() const noexcept { return m_render_resolution; }
    void set_render_resolution(glm::uvec2 render_resolution) { m_render_resolution = render_resolution; }

    size_t refresh_rate() const noexcept { return m_refresh_rate; }
    void set_refresh_rate(size_t refresh_rate) { m_refresh_rate = refresh_rate; }

    size_t logical_gpu_index() const noexcept { return m_logical_gpu_index; }
    void set_logical_gpu_index(size_t logical_gpu_index, LogicalGPUIndexSource source) { m_logical_gpu_index = (logical_gpu_index | (size_t(source) << (sizeof(m_logical_gpu_index) * 4))); }

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

public:

    friend std::ostream& operator<<(std::ostream& stream, const Display& display)
    {
        stream << display.m_name << ", LGPU=" << display.m_logical_gpu_index << ", (";
        stream << display.m_virtual_screen_rect.m_x << " / " << display.m_virtual_screen_rect.m_y << ") [";
        stream << display.m_virtual_screen_rect.m_width << " x " << display.m_virtual_screen_rect.m_height << "] @ ";
        stream << display.m_refresh_rate << " Hz, ";
        stream << "(id=" << toolbox::StlUtils::hex_insert(display.m_nv_display_id);
        stream << ", handle=" << toolbox::StlUtils::hex_insert(display.m_nv_display_handle);
        stream << ", num_pgpus=" << display.m_nv_num_physical_gpus << ", num_mosaic_displays=" << display.m_nv_mosaic_num_displays << ")";
        return stream;
    }

private:

    const std::string       m_name;
    const std::string       m_gpu_association_name;
    const rect_t            m_virtual_screen_rect;

    glm::uvec2              m_render_resolution = { 0, 0 };
    size_t                  m_refresh_rate = 0;
    size_t                  m_logical_gpu_index = INVALID_LOGICAL_GPU_INDEX;

    NvU32                   m_nv_display_id = 0;
    NvDisplayHandle         m_nv_display_handle = nullptr;
    size_t                  m_nv_num_physical_gpus = 0;

    size_t                  m_nv_mosaic_num_displays = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// DisplayConfiguration
//------------------------------------------------------------------------------

class DisplayConfiguration
{
public:

    DisplayConfiguration();

public:

    std::shared_ptr<Display> control_display() const noexcept { return m_control_display; }
    std::shared_ptr<Display> mosaic_display() const noexcept { return m_mosaic_display; }
    std::shared_ptr<Display> openvr_display() const noexcept { return m_openvr_display; }
    bool openvr_display_in_direct_mode() const noexcept { return m_is_openvr_display_in_direct_mode; }

    //------------------------------------------------------------------------------
    // No specific pixel format is required for an affinity (display) context as it
    // does not have a default framebuffer, however we do bind to the mosaic window
    // display context for the final pass and thus must match its pixel format.
    static void create_render_contexts(
        std::pair<HDC, HGLRC>& primary_context,
        std::pair<HDC, HGLRC>& support_context,
        std::shared_ptr<Display> stereo_display,
        const PIXELFORMATDESCRIPTOR& pixel_format_desc,
        int context_version_major,
        int context_version_minor
    );

private:

    std::vector<std::shared_ptr<Display>>       m_displays;

    std::shared_ptr<Display>                    m_primary_display;
    std::shared_ptr<Display>                    m_control_display;
    std::shared_ptr<Display>                    m_mosaic_display;
    std::shared_ptr<Display>                    m_openvr_display;
    bool                                        m_is_openvr_display_in_direct_mode = false;

    //------------------------------------------------------------------------------
    // Get a list of physical displays (monitors) from Windows.
    void enum_displays();

    //------------------------------------------------------------------------------
    // Which display is connected to which (logical) GPU.
    void enum_logical_gpus();

    //------------------------------------------------------------------------------
    // Get Mosaic information for displays.
    void enum_mosaics();

    //------------------------------------------------------------------------------
    // Detect OpenVR HMD.
    rect_t identify_openvr_display(glm::uvec2& render_resolution);

    //------------------------------------------------------------------------------
    // DirectX appears to be the shortest link between LUID and display name so we
    // use it to verify the primary display is indeed attached to the same GPU as
    // the OpenVR HMD.
    void verify_primary_display_connected_to_device(uint64_t vr_device_luid, const rect_t& primary_display_virtual_screen_rect);
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __DISPLAY_CONFIGURATION_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
