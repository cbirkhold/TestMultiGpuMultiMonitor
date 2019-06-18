//
//  OpenVRCompositor.h
//  StereoDisplay
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __OPENVR_DISPLAY_H__
#define __OPENVR_DISPLAY_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <array>

#include "_OpenVRApi.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "StereoDisplay.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// OpenVR implementation of a StereoDisplay/PoseTracker.
//------------------------------------------------------------------------------

class OpenVRCompositor
: public StereoDisplay
, public PoseTracker
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

    OpenVRCompositor(vr::IVRCompositor* const compositor,
                     size_t width,
                     size_t height,
                     vr::EColorSpace color_space,
                     vr::EVRSubmitFlags submit_flags);

    vr::IVRCompositor* compositor() const noexcept { return m_compositor; }

    //------------------------------------------------------------------------------
    // Tracked Poses
public:

    size_t num_render_poses() const noexcept { return m_render_poses.size(); }
    const vr::TrackedDevicePose_t& render_pose(size_t index) const { return m_render_poses[index]; }

    //------------------------------------------------------------------------------
    // [StereoDisplay]
public:

    StereoDrawable_UP wait_next_drawable() const override;
    StereoDrawable_UP wait_next_drawable_for(const std::chrono::microseconds& duration, bool* try_failed) const override;

    //------------------------------------------------------------------------------
    // [PoseTracker]
public:

    void wait_get_poses() override;

    //------------------------------------------------------------------------------
    // {Private}
private:

    class RenderTarget;
    class OpenVRStereoDrawable;

    TrackedDevicePoses                      m_render_poses;

    vr::IVRCompositor* const                m_compositor;       // Cached VR compositor instance
    std::shared_ptr<const RenderTarget>     m_render_target;    // Render target shared with the OpenVRStereoDrawable instance
    const vr::EVRSubmitFlags                m_submit_flags;     // Submit flags for the framebuffers
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __OPENVR_DISPLAY_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
