//
//  Utils.cpp
//  Utils
//
//  Created by Chris Birkhold on 2/5/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cassert>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void
AcquireReleaseWithOwnership::release() const noexcept
{
    std::unique_lock<Mutex> lock(m_mutex, std::try_to_lock);

    if (!lock.owns_lock()) {
        assert(false && "Render target must be release on the same thread that it was acquired on!");
        return;
    }

    assert(m_mutex_owner == std::this_thread::get_id());
    m_mutex_owner = std::thread::id();

    m_mutex.unlock();
}

AcquireReleaseWithOwnership::AcquireResult
AcquireReleaseWithOwnership::acquire(std::unique_lock<Mutex>& lock) const noexcept
{
    const std::thread::id this_thread = std::this_thread::get_id();

    if (m_mutex_owner == this_thread) {
        return AcquireResult::ALREADY_OWNED;
    }

    assert(m_mutex_owner == std::thread::id());
    m_mutex_owner = this_thread;

    lock.release();     // Keep the mutex locked

    return AcquireResult::OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
