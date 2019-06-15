//
//  Utils.h
//  Utils
//
//  Created by Chris Birkhold on 2/5/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __UTILS_H__
#define __UTILS_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <mutex>
#include <thread>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "_OpenGLApi.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Utility for serializing rendering and submission of render targets.
//
// We use this to track if the render target is currently owned by the client or
// the display, and if by the client by which thread. Also, by using a mutex we
// can wait with a timeout.
class AcquireReleaseWithOwnership
{
    //------------------------------------------------------------------------------
    // Types
public:

    //------------------------------------------------------------------------------
    // Enum 'class' with bool cast operator.
    class AcquireResult
    {
    public:

        enum result_e {
            OK = 0,

            // Error: The object is already owned by the current thread. This is usually a
            //        logica error, to avoid a deadlock the operation fails.
            ALREADY_OWNED,

            // Error: Trying to acquire the object failed because it is already owned by
            //        another thread, the timeout expired or the try failed spuriously.
            TRY_FAILED,
        };

        constexpr AcquireResult(result_e value) noexcept : m_value(value) {}

        constexpr operator result_e() const noexcept { return m_value; }
        constexpr operator bool() const noexcept { return (m_value == OK); }

    private:

        const result_e      m_value;
    };

    typedef std::recursive_timed_mutex Mutex;

    //------------------------------------------------------------------------------
    // Acquire/Release
public:

    //------------------------------------------------------------------------------
    // Return true if the render target was successfully acquired or false if the
    // render target is already owned by the current thread. Blocks execution until
    // the lock is acquired.
    AcquireResult acquire() const
    {
        std::unique_lock<Mutex> lock(m_mutex);
        return acquire(lock);
    }

    //------------------------------------------------------------------------------
    // Return true if the render target was successfully acquired or false if the
    // render target is already owned, including by the current thread.
    AcquireResult try_acquire() const noexcept
    {
        std::unique_lock<Mutex> lock(m_mutex, std::try_to_lock);

        if (!lock.owns_lock()) {
            return AcquireResult::TRY_FAILED;
        }

        return acquire(lock);
    }

    //------------------------------------------------------------------------------
    // Return true if the render target was successfully acquired or false if the
    // render target is already owned, including by the current thread. Blocks
    // execution until the lock is acquired or the timeout expires.
    AcquireResult try_acquire_for(const std::chrono::microseconds& duration) const
    {
        std::unique_lock<Mutex> lock(m_mutex, duration);

        if (!lock.owns_lock()) {
            return AcquireResult::TRY_FAILED;
        }

        return acquire(lock);
    }

    //------------------------------------------------------------------------------
    // Release ownership of the render target.
    void release() const noexcept;

    //------------------------------------------------------------------------------
    // Returns true if the render target is owned by the current thread or false
    // otherwise.
    //
    // While it's not meaningful to determine the arbitrary owner of a mutex it is
    // possible to answer the more narrow question if a recursive mutex is owned by
    // the current thread. To do so we try to lock the mutex which will fail if the
    // mutex is owned by a different thread but succeed if the mutex was not owned
    // by any thread or already owned by the current thread.
    bool owned_by_this_thread() const noexcept
    {
        std::unique_lock<Mutex> lock(m_mutex, std::try_to_lock);
        return (lock.owns_lock() && (m_mutex_owner == std::this_thread::get_id()));
    }

    //------------------------------------------------------------------------------
    // [Lockable]
public:

    void lock() const { if (!acquire()) { throw std::runtime_error("Already locked by this thread!"); } }
    bool try_lock() const noexcept { return try_acquire(); }
    void unlock() const noexcept { release(); }

    //------------------------------------------------------------------------------
    // {Private}
private:

    mutable Mutex                   m_mutex;
    mutable std::thread::id         m_mutex_owner;

    AcquireResult acquire(std::unique_lock<Mutex>& lock) const noexcept;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void create_texture_backed_render_targets(GLuint* framebuffers, GLuint* color_attachments, size_t n, size_t width, size_t height);
void delete_texture_backed_render_targets(const GLuint* framebuffers, const GLuint* color_attachments, size_t n);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // __UTILS_H__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
