//
//  RenderPoints.cpp
//  vmi-player
//
//  Created by Chris Birkhold on 2/4/19.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RenderPoints.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <iostream>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "OpenGLUtils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

GLint RenderPoints::s_uniform_location_rect = -1;
GLint RenderPoints::s_uniform_location_mvp = -1;
GLint RenderPoints::s_uniform_location_grid_size = -1;
GLint RenderPoints::s_uniform_location_grid_size_minus_one_recip = -1;
GLint RenderPoints::s_uniform_location_color_mask = -1;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

GLuint RenderPoints::create_program()
{
    static const char* const vs_string =
        "#version 460\n"
        "uniform vec4 u_rect;\n"
        "uniform mat4 u_mvp;\n"
        "uniform int u_grid_size;\n"
        "uniform float u_grid_size_minus_one_recip;\n"
        "out vec2 v_uv;\n"
        "void main() {\n"
        "    int x = (gl_VertexID % u_grid_size);\n"
        "    int y = (gl_VertexID / u_grid_size);\n"
        "    vec2 uv = (vec2(x, y) * u_grid_size_minus_one_recip);\n"
        "    gl_Position = (u_mvp * vec4((u_rect.xy + (uv * u_rect.zw)), 0.0, 1.0));\n"
        "    v_uv = vec2(uv.x, uv.y);\n"
        "}\n";

    static const char* const fs_string =
        "#version 460\n"
        "uniform vec4 u_color_mask;\n"
        "in vec2 v_uv;\n"
        "out vec4 f_color;\n"
        "void main() {\n"
        "    float vignette = pow(clamp(((v_uv.x * (1.0f - v_uv.x)) * (v_uv.y * (1.0f - v_uv.y)) * 36.0f), 0.0, 1.0), 4.0);\n"
        "    f_color = vec4(((v_uv.rg * vignette) * u_color_mask.rg), u_color_mask.b, u_color_mask.a);\n"
        "}\n";

    try {
        const GLuint vertex_shader = toolbox::OpenGLShader::create_from_source(GL_VERTEX_SHADER, vs_string);
        const GLuint fragment_shader = toolbox::OpenGLShader::create_from_source(GL_FRAGMENT_SHADER, fs_string);

        toolbox::OpenGLProgram::attribute_location_list_t attribute_locations;
        toolbox::OpenGLProgram::frag_data_location_list_t frag_data_locations;
        const GLuint program = toolbox::OpenGLProgram::create_from_shaders(vertex_shader, fragment_shader, attribute_locations, frag_data_locations);

        s_uniform_location_rect = glGetUniformLocation(program, "u_rect");
        s_uniform_location_mvp = glGetUniformLocation(program, "u_mvp");
        s_uniform_location_grid_size = glGetUniformLocation(program, "u_grid_size");
        s_uniform_location_grid_size_minus_one_recip = glGetUniformLocation(program, "u_grid_size_minus_one_recip");
        s_uniform_location_color_mask = glGetUniformLocation(program, "u_color_mask");

        return program;
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Failed to load program: Unknown exception!" << std::endl;
    }

    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
