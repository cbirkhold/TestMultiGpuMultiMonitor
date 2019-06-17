//
//  OpenGLUtils.cpp
//  OpenGLUtils
//
//  Created by Chris Birkhold on 8/19/18.
//  Copyright © 2018 Chris Birkhold. All rights reserved.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "OpenGLUtils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cassert>
#include <set>
#include <tuple>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(NDEBUG)
# define TOOLBOX_DEBUG 0
#else
# define TOOLBOX_DEBUG 1
#endif

#define TOOLBOX_LOG_ERROR(...) printf(__VA_ARGS__)
#define TOOLBOX_LOG_WARNING(...) printf(__VA_ARGS__)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

  void
  log_shader_info(GLuint shader)
  {
    GLint info_log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);

    if (info_log_length > 0) {
      std::string info_log(info_log_length, '.');
      glGetShaderInfoLog(shader, info_log_length, &info_log_length, &info_log[0]);
      TOOLBOX_LOG_ERROR("%s", info_log.data());
    }
  }

  void
  log_program_info(GLuint program)
  {
    GLint info_log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);

    if (info_log_length > 0) {
      std::string info_log(info_log_length, '.');
      glGetProgramInfoLog(program, info_log_length, &info_log_length, &info_log[0]);
      TOOLBOX_LOG_ERROR("%s", info_log.data());
    }
  }

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace toolbox {

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  void
  OpenGLFramebuffer::create_texture_backed(
    GLuint* const framebuffers,
    GLuint* const color_attachments,
    GLuint* const depth_attachments,
    size_t n,
    size_t width,
    size_t height)
  {
    glGenFramebuffers(GLsizei(n), framebuffers);
    glGenTextures(GLsizei(n), color_attachments);

    if (depth_attachments) {
      glGenRenderbuffers(GLsizei(n), depth_attachments);
    }

    for (size_t i = 0; i < n; ++i) {
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
      glBindTexture(GL_TEXTURE_2D, color_attachments[i]);

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_attachments[i], 0);

      if (depth_attachments) {
        glBindRenderbuffer(GL_RENDERBUFFER, depth_attachments[i]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, GLsizei(width), GLsizei(height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_attachments[i]);
      }

      const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

      if (status != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Failed to validate framebuffer status!");
      }
    }
  }

  void
  OpenGLFramebuffer::delete_texture_backed(
    GLuint* const framebuffers,
    GLuint* const color_attachments,
    GLuint* const depth_attachments,
    size_t n)
  {
    if (depth_attachments) {
      glDeleteTextures(GLsizei(n), depth_attachments);
      memset(depth_attachments, 0, (n * sizeof(depth_attachments[0])));
    }

    glDeleteTextures(GLsizei(n), color_attachments);
    memset(color_attachments, 0, (n * sizeof(color_attachments[0])));

    glDeleteFramebuffers(GLsizei(n), framebuffers);
    memset(framebuffers, 0, (n * sizeof(framebuffers[0])));
  }

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  GLuint
  OpenGLShader::create_from_source(GLenum type, const std::string& source)
  {
    const GLchar* const sources[] = { source.data() };
    const GLint lengths[] = { GLint(source.length()) };

    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, sources, lengths);
    glCompileShader(shader);

    GLint compile_status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);

    if (compile_status != GL_TRUE) {
      log_shader_info(shader);
      glDeleteShader(shader);
      throw std::runtime_error("Failed to compile shader!");
    }

    return shader;
  }

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

  GLuint
  OpenGLProgram::create_from_shaders(
    GLuint vertex_shader,
    GLuint fragment_shader,
    attribute_location_list_t& attribute_locations,
    frag_data_location_list_t& frag_data_locations)
  {
    std::set<GLint> used_indices;

    //------------------------------------------------------------------------------
    // Create program and attach shaders.
    GLuint program = glCreateProgram();

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);

    glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glProgramParameteri(program, GL_PROGRAM_SEPARABLE, GL_FALSE);

    //------------------------------------------------------------------------------
    // Attempt to bind valid attribute locations (must be done before linking).
    if ((TOOLBOX_DEBUG)) {
      used_indices.clear();
    }

    for (const auto& tuple : attribute_locations) {
      const GLint location = std::get<0>(tuple);
      const std::string& name = std::get<1>(tuple);

      if ((location < 0) || name.empty()) {
        continue;
      }

      if ((TOOLBOX_DEBUG)) {
        if (used_indices.find(location) != used_indices.end()) {
          TOOLBOX_LOG_WARNING("Attribute location %ji was already bound!", intmax_t(location));
        }
      }

      glBindAttribLocation(program, location, name.c_str());

      if ((TOOLBOX_DEBUG)) {
        used_indices.insert(location);
      }
    }

    //------------------------------------------------------------------------------
    // Attempt to bind valid fragment data locations (must be done before linking).
    if ((TOOLBOX_DEBUG)) {
      used_indices.clear();
    }

    for (const auto& tuple : frag_data_locations) {
      const GLint location = std::get<0>(tuple);
      const GLint index = std::get<1>(tuple);
      const std::string& name = std::get<2>(tuple);

      if ((location < 0) || (index < 0) || name.empty()) {
        continue;
      }

      if ((TOOLBOX_DEBUG)) {
        if (used_indices.find(location) != used_indices.end()) {
          TOOLBOX_LOG_WARNING("Fragment data location %ji was already bound!", intmax_t(location));
        }
      }

      glBindFragDataLocationIndexed(program, location, index, name.c_str());

      if ((TOOLBOX_DEBUG)) {
        used_indices.insert(location);
      }
    }

    //------------------------------------------------------------------------------
    // Link program and check result.
    glLinkProgram(program);

    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);

    if (link_status != GL_TRUE) {
      log_program_info(program);
      glDeleteProgram(program);
      throw std::runtime_error("Failed to link binary!");
    }

    //------------------------------------------------------------------------------
    // Check actual attribute locations for the names we have been given.
    GLint num_active_attributes = 0;
    GLint max_attribute_length = 0;   // Includes terminator.

    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &num_active_attributes);
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_attribute_length);

    std::string attribute_name(max_attribute_length, '\0');

    attribute_location_list_t active_attribute_locations;

    for (GLint attribute_index = 0; attribute_index < num_active_attributes; ++attribute_index) {
      GLsizei length = 0;   // Does not include terminator.
      GLint size = 0;
      GLenum type = GL_INVALID_ENUM;

      glGetActiveAttrib(program, attribute_index, max_attribute_length, &length, &size, &type, &attribute_name[0]);
      const GLint location = glGetAttribLocation(program, attribute_name.c_str());

      if (location >= 0) {
        active_attribute_locations.emplace_back(location, attribute_name);
      }
    }

    attribute_locations = std::move(active_attribute_locations);

    //------------------------------------------------------------------------------
    // Check actual fragment data locations for the names we have been given.
    frag_data_location_list_t actual_frag_data_locations;

    for (auto& tuple : frag_data_locations) {
      std::string& name = std::get<2>(tuple);

      if (name.empty()) {
        continue;
      }

      const GLint location = glGetFragDataLocation(program, name.c_str());

      if (location < 0) {
        continue;
      }

      const GLint index = glGetFragDataIndex(program, name.c_str());
      assert((index == 0) || (index == 1));

      actual_frag_data_locations.emplace_back(location, index, std::move(name));
    }

    frag_data_locations = std::move(actual_frag_data_locations);

    //------------------------------------------------------------------------------
    // ...
    return program;
  }

  bool
  OpenGLProgram::validate(GLuint program)
  {
    glValidateProgram(program);

    GLint validate_status = 0;
    glGetProgramiv(program, GL_VALIDATE_STATUS, &validate_status);

    if (validate_status != GL_TRUE) {
      log_program_info(program);
      return false;
    }

    return true;
  }

  ////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////

} // namespace toolbox

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
