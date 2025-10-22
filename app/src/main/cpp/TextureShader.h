#ifndef ANDROIDGLINVESTIGATIONS_TEXTURESHADER_H
#define ANDROIDGLINVESTIGATIONS_TEXTURESHADER_H

#include <string>
#include <GLES3/gl3.h>

class TexturedModel;

/*!
 * A class representing a shader program for textured quads. It consists of vertex and fragment 
 * components. The input attributes are a position (as a Vector3) and texture coordinates (as a Vector2). 
 * It also takes uniforms for the model/view/projection matrix and texture sampler.
 */
class TextureShader {
public:
    /*!
     * Loads a texture shader given the full sourcecode and names for necessary attributes and uniforms to
     * link to. Returns a valid shader on success or null on failure. Shader resources are
     * automatically cleaned up on destruction.
     */
    static TextureShader *loadShader(
            const std::string &vertexSource,
            const std::string &fragmentSource,
            const std::string &positionAttributeName,
            const std::string &texCoordAttributeName,
            const std::string &modelMatrixUniformName,
            const std::string &projectionMatrixUniformName,
            const std::string &textureUniformName);

    inline ~TextureShader() {
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
     * Renders a single textured model
     * @param model a textured model to render
     */
    void drawTexturedModel(const TexturedModel &model) const;

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

    /*!
     * Sets the texture to be used for rendering
     * @param textureId OpenGL texture ID
     */
    void setTexture(GLuint textureId) const;

private:
    /*!
     * Helper function to load a shader of a given type
     */
    static GLuint loadShader(GLenum shaderType, const std::string &shaderSource);

    /*!
     * Constructs a new instance of a texture shader.
     */
    constexpr TextureShader(
            GLuint program,
            GLint position,
            GLint texCoord,
            GLint modelMatrix,
            GLint projectionMatrix,
            GLint texture)
            : program_(program),
              position_(position),
              texCoord_(texCoord),
              modelMatrix_(modelMatrix),
              projectionMatrix_(projectionMatrix),
              texture_(texture) {}

    GLuint program_;
    GLint position_;
    GLint texCoord_;
    GLint modelMatrix_;
    GLint projectionMatrix_;
    GLint texture_;
};

#endif //ANDROIDGLINVESTIGATIONS_TEXTURESHADER_H