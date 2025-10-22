#include "TextureShader.h"

#include "AndroidOut.h"
#include "Model.h"
#include "Utility.h"

TextureShader *TextureShader::loadShader(
        const std::string &vertexSource,
        const std::string &fragmentSource,
        const std::string &positionAttributeName,
        const std::string &texCoordAttributeName,
        const std::string &modelMatrixUniformName,
        const std::string &projectionMatrixUniformName,
        const std::string &textureUniformName) {
    TextureShader *shader = nullptr;

    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader) {
        return nullptr;
    }

    GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return nullptr;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);

        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

            if (logLength) {
                GLchar *log = new GLchar[logLength];
                glGetProgramInfoLog(program, logLength, nullptr, log);
                aout << "Failed to link texture program with:\n" << log << std::endl;
                delete[] log;
            }

            glDeleteProgram(program);
        } else {
            GLint positionAttribute = glGetAttribLocation(program, positionAttributeName.c_str());
            GLint texCoordAttribute = glGetAttribLocation(program, texCoordAttributeName.c_str());
            GLint modelMatrixUniform = glGetUniformLocation(program, modelMatrixUniformName.c_str());
            GLint projectionMatrixUniform = glGetUniformLocation(program, projectionMatrixUniformName.c_str());
            GLint textureUniform = glGetUniformLocation(program, textureUniformName.c_str());

            if (positionAttribute != -1
                && texCoordAttribute != -1
                && modelMatrixUniform != -1
                && projectionMatrixUniform != -1
                && textureUniform != -1) {

                shader = new TextureShader(
                        program,
                        positionAttribute,
                        texCoordAttribute,
                        modelMatrixUniform,
                        projectionMatrixUniform,
                        textureUniform);
            } else {
                aout << "Failed to get texture shader attributes/uniforms" << std::endl;
                glDeleteProgram(program);
            }
        }
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shader;
}

GLuint TextureShader::loadShader(GLenum shaderType, const std::string &shaderSource) {
    Utility::assertGlError();
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        auto *shaderRawString = (GLchar *) shaderSource.c_str();
        GLint shaderLength = shaderSource.length();
        glShaderSource(shader, 1, &shaderRawString, &shaderLength);
        glCompileShader(shader);

        GLint shaderCompiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &shaderCompiled);

        if (!shaderCompiled) {
            GLint infoLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLength);

            if (infoLength) {
                auto *infoLog = new GLchar[infoLength];
                glGetShaderInfoLog(shader, infoLength, nullptr, infoLog);
                aout << "Failed to compile texture shader with:\n" << infoLog << std::endl;
                delete[] infoLog;
            }

            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

void TextureShader::activate() const {
    glUseProgram(program_);
}

void TextureShader::deactivate() const {
    glUseProgram(0);
}

void TextureShader::drawTexturedModel(const TexturedModel &model) const {
    // The position attribute is 3 floats
    glVertexAttribPointer(
            position_,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(TexturedVertex),
            model.getVertexData()
    );
    glEnableVertexAttribArray(position_);

    // The texture coordinate attribute is 2 floats
    glVertexAttribPointer(
            texCoord_,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(TexturedVertex),
            ((uint8_t *) model.getVertexData()) + sizeof(Vector3)
    );
    glEnableVertexAttribArray(texCoord_);

    // Draw as indexed triangles
    glDrawElements(GL_TRIANGLES, model.getIndexCount(), GL_UNSIGNED_SHORT, model.getIndexData());

    glDisableVertexAttribArray(texCoord_);
    glDisableVertexAttribArray(position_);
}

void TextureShader::setModelMatrix(float *modelMatrix) const {
    glUniformMatrix4fv(modelMatrix_, 1, false, modelMatrix);
}

void TextureShader::setProjectionMatrix(float *projectionMatrix) const {
    glUniformMatrix4fv(projectionMatrix_, 1, false, projectionMatrix);
}

void TextureShader::setTexture(GLuint textureId) const {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(texture_, 0);
}