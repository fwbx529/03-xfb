/* $URL$
   $Rev$
   $Author$
   $Date$
   $Id$
 */

#include <stdio.h>
#include <windows.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "LoadShaders.h"
#include "vbm.h"

float aspect;
GLuint update_prog;
GLuint vao[2];
GLuint vbo[2];
GLuint xfb;

GLuint render_prog;
GLuint geometry_vbo;
GLuint render_vao;
GLint render_model_matrix_loc;
GLint render_projection_matrix_loc;

GLuint geometry_tex;

GLuint geometry_xfb;
GLuint particle_xfb;

GLint model_matrix_loc;
GLint projection_matrix_loc;
GLint triangle_count_loc;
GLint time_step_loc;

VBObject object;

ULONGLONG m_appStartTime;

const int point_count = 5000;
static unsigned int seed = 0x13371337;

static inline float random_float()
{
    float res;
    unsigned int tmp;

    seed *= 16807;

    tmp = seed ^ (seed >> 4) ^ (seed << 15);

    *((unsigned int *) &res) = (tmp >> 9) | 0x3F800000;

    return (res - 1.0f);
}

static glm::vec3 random_vector(float minmag = 0.0f, float maxmag = 1.0f)
{
    glm::vec3 randomvec(random_float() * 2.0f - 1.0f, random_float() * 2.0f - 1.0f, random_float() * 2.0f - 1.0f);
    randomvec = normalize(randomvec);
    randomvec *= (random_float() * (maxmag - minmag) + minmag);

    return randomvec;
}

void Initialize()
{
    m_appStartTime = ::GetTickCount64();

    ShaderInfo shader_info_update[] =
    {
        { GL_VERTEX_SHADER, "update.vs.source.glsl" },
        { GL_FRAGMENT_SHADER, "white.fs.glsl" },
        { GL_NONE, NULL }
    };

    update_prog = LoadShaders(shader_info_update);

    static const char * varyings[] =
    {
        "position_out", "velocity_out"
    };

    glTransformFeedbackVaryings(update_prog, 2, varyings, GL_INTERLEAVED_ATTRIBS);

    glLinkProgram(update_prog);
    glUseProgram(update_prog);

    model_matrix_loc = glGetUniformLocation(update_prog, "model_matrix");
    projection_matrix_loc = glGetUniformLocation(update_prog, "projection_matrix");
    triangle_count_loc = glGetUniformLocation(update_prog, "triangle_count");
    time_step_loc = glGetUniformLocation(update_prog, "time_step");

    ShaderInfo shader_info_render[] =
    {
        { GL_VERTEX_SHADER, "render.vs.glsl" },
        { GL_FRAGMENT_SHADER, "blue.fs.glsl" },
        { GL_NONE, NULL }
    };

    render_prog = LoadShaders(shader_info_render);

    static const char * varyings2[] =
    {
        "world_space_position"
    };

    glTransformFeedbackVaryings(render_prog, 1, varyings2, GL_INTERLEAVED_ATTRIBS);

    glLinkProgram(render_prog);
    glUseProgram(render_prog);

    render_model_matrix_loc = glGetUniformLocation(render_prog, "model_matrix");
    render_projection_matrix_loc = glGetUniformLocation(render_prog, "projection_matrix");

    glGenVertexArrays(2, vao);
    glGenBuffers(2, vbo);

    for (int i = 0; i < 2; i++)
    {
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, vbo[i]);
        glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, point_count * (sizeof(glm::vec4) + sizeof(glm::vec3)), NULL, GL_DYNAMIC_COPY);
        if (i == 0)
        {
            struct buffer_t {
                glm::vec4 position;
                glm::vec3 velocity;
            } * buffer = (buffer_t *)glMapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, GL_WRITE_ONLY);

            for (int j = 0; j < point_count; j++)
            {
                buffer[j].velocity = random_vector();
                buffer[j].position = glm::vec4(buffer[j].velocity + glm::vec3(-0.5f, 40.0f, 0.0f), 1.0f);
                buffer[j].velocity = glm::vec3(buffer[j].velocity[0], buffer[j].velocity[1] * 0.3f, buffer[j].velocity[2] * 0.3f);
            }

            glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
        }

        glBindVertexArray(vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4) + sizeof(glm::vec3), NULL);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec4) + sizeof(glm::vec3), (GLvoid *)sizeof(glm::vec4));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }

    glGenBuffers(1, &geometry_vbo);
    glGenTextures(1, &geometry_tex);
    glBindBuffer(GL_TEXTURE_BUFFER, geometry_vbo);
    glBufferData(GL_TEXTURE_BUFFER, 1024 * 1024 * sizeof(glm::vec4), NULL, GL_DYNAMIC_COPY);
    glBindTexture(GL_TEXTURE_BUFFER, geometry_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, geometry_vbo);

    glGenVertexArrays(1, &render_vao);
    glBindVertexArray(render_vao);
    glBindBuffer(GL_ARRAY_BUFFER, geometry_vbo);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0f);

    object.LoadFromVBM("armadillo_low.vbm", 0, 1, 2);
}

static inline int min(int a, int b)
{
    return a < b ? a : b;
}

void Display()
{
    static int frame_count = 0;
    ULONGLONG currentTime = ::GetTickCount64();
    unsigned int app_time = (unsigned int)(currentTime - m_appStartTime);
    float t = float(app_time & 0x3FFFF) / float(0x3FFFF);
    static float q = 0.0f;
    static const glm::vec3 X(1.0f, 0.0f, 0.0f);
    static const glm::vec3 Y(0.0f, 1.0f, 0.0f);
    static const glm::vec3 Z(0.0f, 0.0f, 1.0f);

    glm::mat4 projection_matrix(glm::frustum(-1.0f, 1.0f, -aspect, aspect, 1.0f, 5000.0f) * glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, -100.0f)));
    glm::mat4 model_matrix(glm::scale(glm::mat4(), glm::vec3(0.3f)) *
                           glm::rotate(glm::mat4(), glm::radians(t * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
                           glm::rotate(glm::mat4(), glm::radians(t * 360.0f * 3.0f), glm::vec3(0.0f, 0.0f, 1.0f)));

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glUseProgram(render_prog);
    glUniformMatrix4fv(render_model_matrix_loc, 1, GL_FALSE, &model_matrix[0][0]);
    glUniformMatrix4fv(render_projection_matrix_loc, 1, GL_FALSE, &projection_matrix[0][0]);

    glBindVertexArray(render_vao);

    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, geometry_vbo);

    glBeginTransformFeedback(GL_TRIANGLES);
    object.Render();
    glEndTransformFeedback();

    glUseProgram(update_prog);
    model_matrix = glm::mat4();
    glUniformMatrix4fv(model_matrix_loc, 1, GL_FALSE, &model_matrix[0][0]);
    glUniformMatrix4fv(projection_matrix_loc, 1, GL_FALSE, &projection_matrix[0][0]);
    glUniform1i(triangle_count_loc, object.GetVertexCount() / 3);

    if (t > q)
    {
        glUniform1f(time_step_loc, (t - q) * 2000.0f);
    }

    q = t;

    if ((frame_count & 1) != 0)
    {
        glBindVertexArray(vao[1]);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, vbo[0]);
    }
    else
    {
        glBindVertexArray(vao[0]);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, vbo[1]);
    }

    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, min(point_count, (frame_count >> 3)));
    glEndTransformFeedback();

    glBindVertexArray(0);

    frame_count++;
}

void Finalize(void)
{
    glUseProgram(0);
    glDeleteProgram(update_prog);
    glDeleteVertexArrays(2, vao);
    glDeleteBuffers(2, vbo);
}

int main(int argc, char** argv)
{
    const int width = 800;
    const int height = 600;
    aspect = float(height) / float(width);

    glfwInit();
    GLFWwindow* window = glfwCreateWindow(width, height, "TransformFeedback Example", NULL, NULL);
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    glewInit();

    Initialize();
    while (!glfwWindowShouldClose(window))
    {
        Display();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    Finalize();

    glfwDestroyWindow(window);
    glfwTerminate();
}