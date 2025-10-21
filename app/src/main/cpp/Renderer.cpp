#include "Renderer.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"

//! executes glGetString and outputs the result to logcat
#define PRINT_GL_STRING(s) {aout << #s": "<< glGetString(s) << std::endl;}

/*!
 * @brief if glGetString returns a space separated list of elements, prints each one on a new line
 *
 * This works by creating an istringstream of the input c-style string. Then that is used to create
 * a vector -- each element of the vector is a new element in the input string. Finally a foreach
 * loop consumes this and outputs it to logcat using @a aout
 */
#define PRINT_GL_STRING_AS_LIST(s) { \
std::istringstream extensionStream((const char *) glGetString(s));\
std::vector<std::string> extensionList(\
        std::istream_iterator<std::string>{extensionStream},\
        std::istream_iterator<std::string>());\
aout << #s":\n";\
for (auto& extension: extensionList) {\
    aout << extension << "\n";\
}\
aout << std::endl;\
}

//! Color for cornflower blue. Can be sent directly to glClearColor
// #define CORNFLOWER_BLUE 100 / 255.f, 149 / 255.f, 237 / 255.f, 1

// Vertex shader for grid lines
static const char *vertex = R"vertex(#version 300 es
in vec3 inPosition;
in vec3 inColor;

out vec3 fragColor;

uniform mat4 uModel;
uniform mat4 uProjection;

void main() {
    fragColor = inColor;
    gl_Position = uProjection * uModel * vec4(inPosition, 1.0);
}
)vertex";

// Fragment shader for grid lines
static const char *fragment = R"fragment(#version 300 es
precision mediump float;

in vec3 fragColor;

out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
)fragment";

/*!
 * Half the height of the projection matrix. This gives you a renderable area of height 4 ranging
 * from -2 to 2
 */
static constexpr float kProjectionHalfHeight = 2.f;

/*!
 * The near plane distance for the projection matrix. Since this is an orthographic projection
 * matrix, it's convenient to have negative values for sorting (and avoiding z-fighting at 0).
 */
static constexpr float kProjectionNearPlane = -1.f;

/*!
 * The far plane distance for the projection matrix. Since this is an orthographic porjection
 * matrix, it's convenient to have the far plane equidistant from 0 as the near plane.
 */
static constexpr float kProjectionFarPlane = 1.f;

Renderer::~Renderer() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
}

void Renderer::render() {
    // Check to see if the surface has changed size. This is _necessary_ to do every frame when
    // using immersive mode as you'll get no other notification that your renderable area has
    // changed.
    updateRenderArea();

    // When the renderable area changes, the projection matrix has to also be updated. This is true
    // even if you change from the sample orthographic projection matrix as your aspect ratio has
    // likely changed.
    if (shaderNeedsNewProjectionMatrix_) {
        // a placeholder projection matrix allocated on the stack. Column-major memory layout
        float projectionMatrix[16] = {0};

        // build an orthographic projection matrix for 2d rendering
        Utility::buildOrthographicMatrix(
                projectionMatrix,
                kProjectionHalfHeight,
                float(width_) / height_,
                kProjectionNearPlane,
                kProjectionFarPlane);

        // send the matrix to the shader
        // Note: the shader must be active for this to work. Since we only have one shader for this
        // demo, we can assume that it's active.
        shader_->setProjectionMatrix(projectionMatrix);

        // make sure the matrix isn't generated every frame
        shaderNeedsNewProjectionMatrix_ = false;
    }

    // Create model matrix with scroll translation
    float modelMatrix[16] = {0};
    Utility::buildTranslationMatrix(modelMatrix, scrollX_, scrollY_);
    shader_->setModelMatrix(modelMatrix);

    // clear the color buffer
    glClear(GL_COLOR_BUFFER_BIT);

    // Render all the models. There's no depth testing in this sample so they're accepted in the
    // order provided. But the sample EGL setup requests a 24 bit depth buffer so you could
    // configure it at the end of initRenderer
    if (!models_.empty()) {
        for (const auto &model: models_) {
            shader_->drawModel(model);
        }
    }

    // Present the rendered image. This is an implicit glFlush.
    auto swapResult = eglSwapBuffers(display_, surface_);
    assert(swapResult == EGL_TRUE);
}

void Renderer::initRenderer() {
    // Choose your render attributes
    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    // The default display is probably what you want on Android
    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);

    // figure out how many configs there are
    EGLint numConfigs;
    eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);

    // get the list of configurations
    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);

    // Find a config we like.
    // Could likely just grab the first if we don't care about anything else in the config.
    // Otherwise hook in your own heuristic
    auto config = *std::find_if(
            supportedConfigs.get(),
            supportedConfigs.get() + numConfigs,
            [&display](const EGLConfig &config) {
                EGLint red, green, blue, depth;
                if (eglGetConfigAttrib(display, config, EGL_RED_SIZE, &red)
                    && eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &green)
                    && eglGetConfigAttrib(display, config, EGL_BLUE_SIZE, &blue)
                    && eglGetConfigAttrib(display, config, EGL_DEPTH_SIZE, &depth)) {

                    aout << "Found config with " << red << ", " << green << ", " << blue << ", "
                         << depth << std::endl;
                    return red == 8 && green == 8 && blue == 8 && depth == 24;
                }
                return false;
            });

    aout << "Found " << numConfigs << " configs" << std::endl;
    aout << "Chose " << config << std::endl;

    // create the proper window surface
    EGLint format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    EGLSurface surface = eglCreateWindowSurface(display, config, app_->window, nullptr);

    // Create a GLES 3 context
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);

    // get some window metrics
    auto madeCurrent = eglMakeCurrent(display, surface, surface, context);
    assert(madeCurrent);

    display_ = display;
    surface_ = surface;
    context_ = context;

    // make width and height invalid so it gets updated the first frame in @a updateRenderArea()
    width_ = -1;
    height_ = -1;

    PRINT_GL_STRING(GL_VENDOR);
    PRINT_GL_STRING(GL_RENDERER);
    PRINT_GL_STRING(GL_VERSION);
    PRINT_GL_STRING_AS_LIST(GL_EXTENSIONS);

    shader_ = std::unique_ptr<Shader>(
            Shader::loadShader(vertex, fragment, "inPosition", "inColor", "uModel", "uProjection"));
    assert(shader_);

    // Note: there's only one shader in this demo, so I'll activate it here. For a more complex game
    // you'll want to track the active shader and activate/deactivate it as necessary
    shader_->activate();

    // setup any other gl related global states
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Black background for better grid visibility

    // enable alpha globally for now, you probably don't want to do this in a game
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // get some demo models into memory
    createModels();
}

void Renderer::updateRenderArea() {
    EGLint width;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);

    EGLint height;
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);

    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glViewport(0, 0, width, height);

        // make sure that we lazily recreate the projection matrix before we render
        shaderNeedsNewProjectionMatrix_ = true;
    }
}

/**
 * @brief Create a 2D grid for rendering.
 */
void Renderer::createModels() {
    // Grid parameters
    const int gridSize = 10; // 10x10 grid
    const float gridSpacing = 0.4f; // Space between grid lines
    const float gridExtent = gridSize * gridSpacing * 0.5f; // Half the total grid size
    
    std::vector<Vertex> vertices;
    std::vector<Index> indices;
    
    // White color for grid lines
    Vector3 gridColor = {1.0f, 1.0f, 1.0f};
    
    // Create vertical lines
    for (int i = 0; i <= gridSize; i++) {
        float x = -gridExtent + i * gridSpacing;
        
        // Add vertices for vertical line
        vertices.emplace_back(Vector3{x, -gridExtent, 0}, gridColor);
        vertices.emplace_back(Vector3{x, gridExtent, 0}, gridColor);
        
        // Add indices for this line
        Index baseIndex = (i * 2);
        indices.push_back(baseIndex);
        indices.push_back(baseIndex + 1);
    }
    
    // Create horizontal lines
    for (int i = 0; i <= gridSize; i++) {
        float y = -gridExtent + i * gridSpacing;
        
        // Add vertices for horizontal line
        vertices.emplace_back(Vector3{-gridExtent, y, 0}, gridColor);
        vertices.emplace_back(Vector3{gridExtent, y, 0}, gridColor);
        
        // Add indices for this line
        Index baseIndex = ((gridSize + 1) * 2) + (i * 2);
        indices.push_back(baseIndex);
        indices.push_back(baseIndex + 1);
    }

    // Create a model and put it in the back of the render list.
    models_.emplace_back(vertices, indices);
}

void Renderer::handleInput() {
    // handle all queued inputs
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) {
        // no inputs yet.
        return;
    }

    // handle motion events (motionEventsCounts can be 0).
    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];
        auto action = motionEvent.action;

        // Find the pointer index, mask and bitshift to turn it into a readable value.
        auto pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        // get the x and y position of this event if it is not ACTION_MOVE.
        auto &pointer = motionEvent.pointers[pointerIndex];
        auto x = GameActivityPointerAxes_getX(&pointer);
        auto y = GameActivityPointerAxes_getY(&pointer);

        // Convert screen coordinates to normalized coordinates
        float normalizedX = (x / float(width_)) * 2.0f - 1.0f;
        float normalizedY = -((y / float(height_)) * 2.0f - 1.0f); // Flip Y axis

        // determine the action type and process the event accordingly.
        switch (action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                lastTouchX_ = normalizedX;
                lastTouchY_ = normalizedY;
                isScrolling_ = true;
                aout << "Touch Down: (" << normalizedX << ", " << normalizedY << ")" << std::endl;
                break;

            case AMOTION_EVENT_ACTION_CANCEL:
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                isScrolling_ = false;
                aout << "Touch Up: (" << normalizedX << ", " << normalizedY << ")" << std::endl;
                break;

            case AMOTION_EVENT_ACTION_MOVE:
                if (isScrolling_) {
                    // Calculate the delta movement
                    float deltaX = normalizedX - lastTouchX_;
                    float deltaY = normalizedY - lastTouchY_;
                    
                    // Update scroll position
                    scrollX_ += deltaX * 2.0f; // Scale factor for sensitivity
                    scrollY_ += deltaY * 2.0f;
                    
                    // Update last touch position
                    lastTouchX_ = normalizedX;
                    lastTouchY_ = normalizedY;
                    
                    aout << "Scroll: (" << scrollX_ << ", " << scrollY_ << ")" << std::endl;
                }
                break;
            default:
                aout << "Unknown MotionEvent Action: " << action << std::endl;
        }
    }
    // clear the motion input count in this buffer for main thread to re-use.
    android_app_clear_motion_events(inputBuffer);

    // handle input key events.
    for (auto i = 0; i < inputBuffer->keyEventsCount; i++) {
        auto &keyEvent = inputBuffer->keyEvents[i];
        aout << "Key: " << keyEvent.keyCode <<" ";
        switch (keyEvent.action) {
            case AKEY_EVENT_ACTION_DOWN:
                aout << "Key Down";
                break;
            case AKEY_EVENT_ACTION_UP:
                aout << "Key Up";
                break;
            case AKEY_EVENT_ACTION_MULTIPLE:
                // Deprecated since Android API level 29.
                aout << "Multiple Key Actions";
                break;
            default:
                aout << "Unknown KeyEvent Action: " << keyEvent.action;
        }
        aout << std::endl;
    }
    // clear the key input count too.
    android_app_clear_key_events(inputBuffer);
}