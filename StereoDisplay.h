//
//  StereoDisplay.h
//  StereoDisplay
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __STEREO_DISPLAY_H__
#define __STEREO_DISPLAY_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <chrono>
#include <memory>

#include "_GLMApi.h"
#include "_OpenGLApi.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Minimal interface to an OpenGL stereo display drawable.
//
// Access to a framebuffer for each eye (which may be the same for both) and a
// viewport into each (or the shared) framebuffer is provided. After rendering
// to a drawable is complete submit() must be called.
//
// See StereoDisplay::wait_next_drawable() for details on requesting a drawable.
//------------------------------------------------------------------------------

class StereoDrawable
{
public:

    enum EyeIndex_e {
        EYE_INDEX_LEFT  = 0,
        EYE_INDEX_RIGHT = 1,
    };

public:

    virtual ~StereoDrawable() {}

protected:

    StereoDrawable() {}

public:

    //------------------------------------------------------------------------------
    // Return the framebuffer name associated with the given eye. If the drawable
    // itself or the eye index are invalid return -1. If a valid framebuffer is
    // returned a valid viewport must also be available. An implementation may
    // return the same name for both eyes if they share a single framebuffer.
    virtual GLuint framebuffer(size_t eye_index) const noexcept = 0;

    //------------------------------------------------------------------------------
    // Return the viewport into the framebuffer associated with the given eye in
    // pixels. If the drawable itself or the eye index are invalid return all zeros.
    // If a valid viewport is returned a valid framebuffer must also be available.
    virtual glm::ivec4 viewport(size_t eye_index) const noexcept = 0;

    //------------------------------------------------------------------------------
    // Bind the framebuffer with the given index and return true if successful or
    // false otherwise. If a single framebuffer is shared between the eyes the
    // scissor rect must be set to match the viewport and scissor testing enabled,
    // otherwise setting the scissor is optional.
    virtual bool bind(size_t eye_index, bool always_set_scissor = false) noexcept
    {
        const GLuint framebuffer_ = framebuffer(eye_index);

        if (framebuffer_ == GLuint(-1)) {
            return false;
        }

        glm::ivec4 viewport_ = viewport(eye_index);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
        glViewport(viewport_.x, viewport_.y, viewport_.z, viewport_.w);

        if (always_set_scissor || (framebuffer_ == framebuffer(eye_index == 0 ? 1 : 0))) {
            glScissor(viewport_.x, viewport_.y, viewport_.z, viewport_.w);
            glEnable(GL_SCISSOR_TEST);
        }
        else {
            glDisable(GL_SCISSOR_TEST);
        }

        return true;
    }

public:

    //------------------------------------------------------------------------------
    // Submit the drawable for display.
    virtual void submit() = 0;
};

typedef std::unique_ptr<StereoDrawable> StereoDrawable_UP;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Minimal interface to a stereo display.
//
// Implementations are free to not return another drawable until the previously
// requested one was submitted if fully serialized rendering is required.
//
// The drawable may only conceptually be a drawable but not the actual final
// drawable presented to the display. Instead the drawable may be used as input
// to a post-processing step involving a distortion and/or color correction
// transform into the final surface.
//------------------------------------------------------------------------------

class StereoDisplay
{
public:

    virtual ~StereoDisplay() {}

protected:

    StereoDisplay() {}

public:

    virtual glm::mat4 projection_matrix(size_t eye_index, double near_z, double far_z) const noexcept = 0;

    virtual StereoDrawable_UP wait_next_drawable() const
    {
        StereoDrawable_UP drawable;
        bool try_failed = true;

        while (try_failed) {
            drawable = wait_next_drawable_for(std::chrono::seconds(1), &try_failed);
            assert(!(drawable && try_failed));
        }

        return drawable;
    }

    virtual StereoDrawable_UP wait_next_drawable_for(const std::chrono::microseconds& duration, bool* try_failed = nullptr) const = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Minimal interface to a pose tracker (such as an HMD and its controllers).
//
// Enables waiting for the next display pose to become available. This would
// usually be called right before requesting the next drawable to obtain the
// most accurate display pose for rendering the next frame.
//------------------------------------------------------------------------------

class PoseTracker
{
public:

    virtual ~PoseTracker() {}

protected:

    PoseTracker() {}

public:

    virtual void wait_get_poses() = 0;
    virtual const glm::mat4 hmd_pose() const noexcept = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __STEREO_DISPLAY_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
