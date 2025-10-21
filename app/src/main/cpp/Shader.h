#ifndef ANDROIDGLINVESTIGATIONS_SHADER_H
#define ANDROIDGLINVESTIGATIONS_SHADER_H

#include <string>
#include <GLES3/gl3.h>

class Model;

/*!
 * A class representing a simple shader program for grid lines. It consists of vertex and fragment 
 * components. The input attributes are a position (as a Vector3) and a color (as a Vector3). 
 * It also takes a uniform to be used as the entire model/view/projection matrix. The shader 
 * renders colored lines without textures.
 */
class Shader {
public:
    /*!
     * Loads a shader given the full sourcecode and names for necessary attributes and uniforms to
     * link to. Returns a valid shader on success or null on failure. Shader resources are
     * automatically cleaned up on destruction.
     *
     * @param vertexSource The full source code for your vertex program
     * @param fragmentSource The full source code of your fragment program
     * @param positionAttributeName The name of the position attribute in your vertex program
     * @param colorAttributeName The name of the color attribute in your vertex program
     * @param modelMatrixUniformName The name of your model matrix uniform
     * @param projectionMatrixUniformName The name of your projection matrix uniform
     * @return a valid Shader on success, otherwise null.
     */
    static Shader *loadShader(
            const std::string &vertexSource,
            const std::string &fragmentSource,
            const std::string &positionAttributeName,
            const std::string &colorAttributeName,
            const std::string &modelMatrixUniformName,
            const std::string &projectionMatrixUniformName);

    inline ~Shader() {
        if (program_) {
            glDeleteProgram(program_);
            program_ = 0;
        }
    }

    /*!
     * Prepares the shader for use, call this before executing any draw commands
     */
    void activate() const;

    /*!
     * Cleans up the shader after use, call this after executing any draw commands
     */
    void deactivate() const;

    /*!
     * Renders a single model
     * @param model a model to render
     */
    void drawModel(const Model &model) const;

    /*!
     * Sets the model matrix in the shader.
     * @param modelMatrix sixteen floats, column major, defining an OpenGL model matrix.
     */
    void setModelMatrix(float *modelMatrix) const;

    /*!
     * Sets the projection matrix in the shader.
     * @param projectionMatrix sixteen floats, column major, defining an OpenGL projection matrix.
     */
    void setProjectionMatrix(float *projectionMatrix) const;

private:
    /*!
     * Helper function to load a shader of a given type
     * @param shaderType The OpenGL shader type. Should either be GL_VERTEX_SHADER or GL_FRAGMENT_SHADER
     * @param shaderSource The full source of the shader
     * @return the id of the shader, as returned by glCreateShader, or 0 in the case of an error
     */
    static GLuint loadShader(GLenum shaderType, const std::string &shaderSource);

    /*!
     * Constructs a new instance of a shader. Use @a loadShader
     * @param program the GL program id of the shader
     * @param position the attribute location of the position
     * @param color the attribute location of the color
     * @param modelMatrix the uniform location of the model matrix
     * @param projectionMatrix the uniform location of the projection matrix
     */
    constexpr Shader(
            GLuint program,
            GLint position,
            GLint color,
            GLint modelMatrix,
            GLint projectionMatrix)
            : program_(program),
              position_(position),
              color_(color),
              modelMatrix_(modelMatrix),
              projectionMatrix_(projectionMatrix) {}

    GLuint program_;
    GLint position_;
    GLint color_;
    GLint modelMatrix_;
    GLint projectionMatrix_;
};

#endif //ANDROIDGLINVESTIGATIONS_SHADER_H