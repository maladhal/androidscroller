#include "Renderer.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>
#include <android/bitmap.h>
#include <jni.h>
#include <cmath>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"
#include "NetworkDownloader.h"

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

// Vertex shader for textured quads
static const char *textureVertex = R"vertex(#version 300 es
in vec3 inPosition;
in vec2 inTexCoord;

out vec2 fragTexCoord;

uniform mat4 uModel;
uniform mat4 uProjection;

void main() {
    fragTexCoord = inTexCoord;
    gl_Position = uProjection * uModel * vec4(inPosition, 1.0);
}
)vertex";

// Fragment shader for textured quads
static const char *textureFragment = R"fragment(#version 300 es
precision mediump float;

in vec2 fragTexCoord;

out vec4 outColor;

uniform sampler2D uTexture;

void main() {
    outColor = texture(uTexture, fragTexCoord);
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
        updateProjectionMatrixWithZoom();
        shaderNeedsNewProjectionMatrix_ = false;
    }

    // Create model matrix with scroll translation
    memset(modelMatrix_, 0, sizeof(modelMatrix_));
    Utility::buildTranslationMatrix(modelMatrix_, scrollX_, scrollY_);
    shader_->setModelMatrix(modelMatrix_);

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
    
    // Render triangle models with triangle shader
    if (!triangleModels_.empty()) {
        triangleShader_->activate();
        triangleShader_->setProjectionMatrix(projectionMatrix_);
        triangleShader_->setModelMatrix(modelMatrix_);
        
        for (const auto &model: triangleModels_) {
            triangleShader_->drawTriangles(model);
        }
        
        shader_->activate(); // Switch back to line shader
    }
    
    // Render textured models (tanks) with texture shader
    if (!texturedModels_.empty() && tankTextureLoaded_) {
        textureShader_->activate();
        textureShader_->setProjectionMatrix(projectionMatrix_);
        textureShader_->setModelMatrix(modelMatrix_);
        textureShader_->setTexture(tankTextureId_);
        
        for (const auto &model: texturedModels_) {
            textureShader_->drawTexturedModel(model);
        }
        
        shader_->activate(); // Switch back to line shader
    }
    
    // Render highlight overlay on top of everything
    if (!highlightModels_.empty()) {
        // Use line shader for highlight border, keep current matrices
        for (const auto &model: highlightModels_) {
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

    triangleShader_ = std::unique_ptr<Shader>(
            Shader::loadShader(vertex, fragment, "inPosition", "inColor", "uModel", "uProjection"));
    assert(triangleShader_);

    textureShader_ = std::unique_ptr<TextureShader>(
            TextureShader::loadShader(textureVertex, textureFragment, "inPosition", "inTexCoord", "uModel", "uProjection", "uTexture"));
    assert(textureShader_);

    // Note: there's only one shader in this demo, so I'll activate it here. For a more complex game
    // you'll want to track the active shader and activate/deactivate it as necessary
    shader_->activate();

    // setup any other gl related global states
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Black background for better grid visibility

    // enable alpha globally for now, you probably don't want to do this in a game
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Initialize texture variables
    tankTextureId_ = 0;
    tankTextureWidth_ = 0;
    tankTextureHeight_ = 0;
    tankTextureLoaded_ = false;

    // get some demo models into memory
    createModels();
    
    // Download map data in a separate thread to avoid blocking
    downloadMapData();
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
    // Create basic grid first - this will be replaced when map data loads
    createColoredGrid();
}

void Renderer::downloadMapData() {
    aout << "Starting map data download..." << std::endl;
    
    // Download JSON map data from the new endpoint
    if (NetworkDownloader::downloadJSON("http://nasmo2.myqnapcloud.com:8585/tanks/index.php", mapData_)) {
        aout << "Map JSON downloaded successfully" << std::endl;
        
        // Download tank image
        if (NetworkDownloader::downloadImage("http://nasmo2.myqnapcloud.com:8585/maps/tank.png", tankImageData_)) {
            aout << "Tank image downloaded successfully" << std::endl;
            
            // Decode PNG to texture
            if (decodePNGToTexture()) {
                aout << "Tank texture loaded successfully" << std::endl;
            } else {
                aout << "Failed to decode tank PNG, using colored squares" << std::endl;
            }
            
            mapDataLoaded_ = true;
            
            // Debug: Print the entire map for analysis
            aout << "=== MAP DATA DEBUG ===" << std::endl;
            aout << "Map size: " << mapData_.width << "x" << mapData_.height << std::endl;
            aout << "Total cells: " << mapData_.data.size() << std::endl;
            aout << "Map content:" << std::endl;
            for (int y = 0; y < mapData_.height; y++) {
                std::string row = "";
                for (int x = 0; x < mapData_.width; x++) {
                    char cell = mapData_.data[y * mapData_.width + x];
                    row += cell;
                    if (cell == 'x' || cell == 'X') {
                        aout << "Tank found at (" << x << ", " << y << ")" << std::endl;
                    }
                    if (cell == 'o' || cell == 'O') {
                        aout << "Object found at (" << x << ", " << y << ")" << std::endl;
                    }
                }
                aout << "Row " << y << ": '" << row << "'" << std::endl;
            }
            aout << "=== END MAP DEBUG ===" << std::endl;
            
            // Recreate models with map data
            models_.clear();
            createColoredGrid();
        } else {
            aout << "Failed to download tank image, using fallback data" << std::endl;
            createFallbackMapData();
        }
    } else {
        aout << "Failed to download map JSON, using fallback data" << std::endl;
        createFallbackMapData();
    }
}

void Renderer::createFallbackMapData() {
    aout << "Creating fallback map data for demonstration" << std::endl;
    
    // Create a test map with various cell types including tanks (x) and objects (o)
    mapData_.width = 10;
    mapData_.height = 10;
    mapData_.data = {
        'x', 'x', ' ', 'x', ' ', ' ', ' ', ' ', ' ', 'x',
        ' ', ' ', 'o', 'o', ' ', ' ', 'x', ' ', ' ', ' ',
        ' ', ' ', ' ', ' ', ' ', 'x', ' ', ' ', ' ', ' ',
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'x', ' ', ' ',
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'x', ' ',
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'x', ' ', ' ',
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
        ' ', ' ', ' ', 'x', ' ', ' ', ' ', ' ', ' ', ' ',
        ' ', ' ', ' ', ' ', 'x', ' ', ' ', ' ', ' ', ' '
    };
    
    mapDataLoaded_ = true;
    
    // Recreate models with fallback data
    models_.clear();
    createColoredGrid();
    
    aout << "Fallback map created: " << mapData_.width << "x" << mapData_.height << " with tank positions ('x') and objects ('o')" << std::endl;
}

void Renderer::createColoredGrid() {
    // Grid parameters
    const int gridSize = mapDataLoaded_ ? std::max(mapData_.width, mapData_.height) : 10;
    const float gridSpacing = 0.4f; // Space between grid lines
    const float gridExtent = gridSize * gridSpacing * 0.5f; // Half the total grid size
    
    std::vector<Vertex> lineVertices;
    std::vector<Index> lineIndices;
    std::vector<Vertex> triangleVertices;
    std::vector<Index> triangleIndices;
    std::vector<TexturedVertex> texturedVertices;
    std::vector<Index> texturedIndices;
    
    // Default white color for grid lines
    Vector3 gridColor = {1.0f, 1.0f, 1.0f};
    
    if (mapDataLoaded_) {
        aout << "Creating colored grid with map data: " << mapData_.width << "x" << mapData_.height << std::endl;
        
        // Create grid cells based on map data
        for (int y = 0; y < mapData_.height; y++) {
            for (int x = 0; x < mapData_.width; x++) {
                char cellType = mapData_.data[y * mapData_.width + x];
                Vector3 cellColor = {0.5f, 0.5f, 0.5f}; // Default gray
                
                // Color code based on cell content
                switch (cellType) {
                    case 'x':
                    case 'X':
                        cellColor = {1.0f, 0.0f, 0.0f}; // Red for tank positions
                        break;
                    case 'o':
                    case 'O':
                        cellColor = {1.0f, 0.5f, 0.0f}; // Orange for objects
                        break;
                    case '1':
                        cellColor = {0.0f, 1.0f, 0.0f}; // Green
                        break;
                    case '2':
                        cellColor = {0.0f, 0.0f, 1.0f}; // Blue
                        break;
                    case '3':
                        cellColor = {1.0f, 1.0f, 0.0f}; // Yellow
                        break;
                    case ' ':
                    default:
                        cellColor = {0.2f, 0.2f, 0.2f}; // Dark gray for empty
                        break;
                }
                
                // Calculate cell position
                float cellX = -gridExtent + (x + 0.5f) * gridSpacing;
                float cellY = gridExtent - (y + 0.5f) * gridSpacing;
                float cellSize = gridSpacing * 0.8f; // Make cells slightly smaller than grid spacing
                
                // For tank positions, create textured quads; for objects, create filled squares; for others, just outline
                if (cellType == 'x' || cellType == 'X') {
                    // Create a textured quad for tank position
                    Index baseIndex = texturedVertices.size();
                    
                    // Add vertices with texture coordinates for tank texture
                    texturedVertices.emplace_back(Vector3{cellX - cellSize/2, cellY + cellSize/2, 0}, Vector2{0.0f, 0.0f}); // Top-left
                    texturedVertices.emplace_back(Vector3{cellX + cellSize/2, cellY + cellSize/2, 0}, Vector2{1.0f, 0.0f}); // Top-right
                    texturedVertices.emplace_back(Vector3{cellX + cellSize/2, cellY - cellSize/2, 0}, Vector2{1.0f, 1.0f}); // Bottom-right
                    texturedVertices.emplace_back(Vector3{cellX - cellSize/2, cellY - cellSize/2, 0}, Vector2{0.0f, 1.0f}); // Bottom-left
                    
                    // Add triangle indices for textured quad (two triangles)
                    texturedIndices.push_back(baseIndex);     texturedIndices.push_back(baseIndex + 1); texturedIndices.push_back(baseIndex + 2); // First triangle
                    texturedIndices.push_back(baseIndex);     texturedIndices.push_back(baseIndex + 2); texturedIndices.push_back(baseIndex + 3); // Second triangle
                } else if (cellType == 'o' || cellType == 'O') {
                    // Create filled triangles for objects
                    Index baseIndex = triangleVertices.size();
                    
                    // Add vertices for filled square (two triangles)
                    triangleVertices.emplace_back(Vector3{cellX - cellSize/2, cellY + cellSize/2, 0}, cellColor); // Top-left
                    triangleVertices.emplace_back(Vector3{cellX + cellSize/2, cellY + cellSize/2, 0}, cellColor); // Top-right
                    triangleVertices.emplace_back(Vector3{cellX + cellSize/2, cellY - cellSize/2, 0}, cellColor); // Bottom-right
                    triangleVertices.emplace_back(Vector3{cellX - cellSize/2, cellY - cellSize/2, 0}, cellColor); // Bottom-left
                    
                    // Add triangle indices for filled quad (two triangles)
                    triangleIndices.push_back(baseIndex);     triangleIndices.push_back(baseIndex + 1); triangleIndices.push_back(baseIndex + 2); // First triangle
                    triangleIndices.push_back(baseIndex);     triangleIndices.push_back(baseIndex + 2); triangleIndices.push_back(baseIndex + 3); // Second triangle
                } else {
                    // Create outline for other cell types
                    Index baseIndex = lineVertices.size();
                    
                    // Add vertices for cell corners
                    lineVertices.emplace_back(Vector3{cellX - cellSize/2, cellY + cellSize/2, 0}, cellColor); // Top-left
                    lineVertices.emplace_back(Vector3{cellX + cellSize/2, cellY + cellSize/2, 0}, cellColor); // Top-right
                    lineVertices.emplace_back(Vector3{cellX + cellSize/2, cellY - cellSize/2, 0}, cellColor); // Bottom-right
                    lineVertices.emplace_back(Vector3{cellX - cellSize/2, cellY - cellSize/2, 0}, cellColor); // Bottom-left
                    
                    // Add indices for cell outline (4 lines)
                    lineIndices.push_back(baseIndex);     lineIndices.push_back(baseIndex + 1); // Top
                    lineIndices.push_back(baseIndex + 1); lineIndices.push_back(baseIndex + 2); // Right
                    lineIndices.push_back(baseIndex + 2); lineIndices.push_back(baseIndex + 3); // Bottom
                    lineIndices.push_back(baseIndex + 3); lineIndices.push_back(baseIndex);     // Left
                }
            }
        }
    } else {
        // Create basic white grid lines as before
        aout << "Creating basic grid (no map data)" << std::endl;
        
        // Create vertical lines
        for (int i = 0; i <= gridSize; i++) {
            float x = -gridExtent + i * gridSpacing;
            
            // Add vertices for vertical line
            lineVertices.emplace_back(Vector3{x, -gridExtent, 0}, gridColor);
            lineVertices.emplace_back(Vector3{x, gridExtent, 0}, gridColor);
            
            // Add indices for this line
            Index baseIndex = (i * 2);
            lineIndices.push_back(baseIndex);
            lineIndices.push_back(baseIndex + 1);
        }
        
        // Create horizontal lines
        for (int i = 0; i <= gridSize; i++) {
            float y = -gridExtent + i * gridSpacing;
            
            // Add vertices for horizontal line
            lineVertices.emplace_back(Vector3{-gridExtent, y, 0}, gridColor);
            lineVertices.emplace_back(Vector3{gridExtent, y, 0}, gridColor);
            
            // Add indices for this line
            Index baseIndex = ((gridSize + 1) * 2) + (i * 2);
            lineIndices.push_back(baseIndex);
            lineIndices.push_back(baseIndex + 1);
        }
    }

    // Clear existing models
    models_.clear();
    triangleModels_.clear();
    texturedModels_.clear();
    highlightModels_.clear();

    // Create models for lines if we have any
    if (!lineVertices.empty()) {
        models_.emplace_back(lineVertices, lineIndices);
        aout << "Created line model with " << lineVertices.size() << " vertices and " << lineIndices.size() << " indices" << std::endl;
    }
    
    // Create models for triangles if we have any  
    if (!triangleVertices.empty()) {
        triangleModels_.emplace_back(triangleVertices, triangleIndices);
        aout << "Created triangle model with " << triangleVertices.size() << " vertices and " << triangleIndices.size() << " indices" << std::endl;
    }
    
    // Create models for textured quads (tanks) if we have any
    if (!texturedVertices.empty()) {
        texturedModels_.emplace_back(texturedVertices, texturedIndices);
        aout << "Created textured model with " << texturedVertices.size() << " vertices and " << texturedIndices.size() << " indices" << std::endl;
    }
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

        int pointerCount = motionEvent.pointerCount;

        // determine the action type and process the event accordingly.
        switch (action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_DOWN: {
                // First finger down
                auto &pointer = motionEvent.pointers[0];
                auto x = GameActivityPointerAxes_getX(&pointer);
                auto y = GameActivityPointerAxes_getY(&pointer);
                
                convertScreenToWorld(x, y, touch1_.x, touch1_.y);
                touch1_.active = true;
                touch2_.active = false;
                isPinching_ = false;
                isScrolling_ = true;
                
                lastTouchX_ = touch1_.x;
                lastTouchY_ = touch1_.y;
                
                // Check for tank selection on single touch down
                checkTankSelection(touch1_.x, touch1_.y);
                
                aout << "Touch Down: (" << touch1_.x << ", " << touch1_.y << ")" << std::endl;
                break;
            }
            
            case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                // Second finger down - start pinch gesture
                if (pointerCount >= 2) {
                    auto &pointer1 = motionEvent.pointers[0];
                    auto &pointer2 = motionEvent.pointers[1];
                    
                    float x1 = GameActivityPointerAxes_getX(&pointer1);
                    float y1 = GameActivityPointerAxes_getY(&pointer1);
                    float x2 = GameActivityPointerAxes_getX(&pointer2);
                    float y2 = GameActivityPointerAxes_getY(&pointer2);
                    
                    convertScreenToWorld(x1, y1, touch1_.x, touch1_.y);
                    convertScreenToWorld(x2, y2, touch2_.x, touch2_.y);
                    
                    touch1_.active = true;
                    touch2_.active = true;
                    isPinching_ = true;
                    isScrolling_ = false;
                    
                    lastPinchDistance_ = calculateDistance(touch1_.x, touch1_.y, touch2_.x, touch2_.y);
                    
                    aout << "Pinch Start: distance=" << lastPinchDistance_ << std::endl;
                }
                break;
            }

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL: {
                // All fingers up
                touch1_.active = false;
                touch2_.active = false;
                isPinching_ = false;
                isScrolling_ = false;
                aout << "All Touch Up" << std::endl;
                break;
            }
            
            case AMOTION_EVENT_ACTION_POINTER_UP: {
                // One finger up - end pinch, potentially start scroll
                if (isPinching_) {
                    isPinching_ = false;
                    
                    // Determine which finger is still down
                    if (pointerIndex == 0) {
                        // First finger up, second still down
                        auto &pointer = motionEvent.pointers[1];
                        auto x = GameActivityPointerAxes_getX(&pointer);
                        auto y = GameActivityPointerAxes_getY(&pointer);
                        convertScreenToWorld(x, y, touch1_.x, touch1_.y);
                        touch2_.active = false;
                    } else {
                        // Second finger up, first still down
                        auto &pointer = motionEvent.pointers[0];
                        auto x = GameActivityPointerAxes_getX(&pointer);
                        auto y = GameActivityPointerAxes_getY(&pointer);
                        convertScreenToWorld(x, y, touch1_.x, touch1_.y);
                        touch2_.active = false;
                    }
                    
                    lastTouchX_ = touch1_.x;
                    lastTouchY_ = touch1_.y;
                    isScrolling_ = true;
                    
                    aout << "Pinch End - Switch to scroll" << std::endl;
                } else {
                    touch1_.active = false;
                    touch2_.active = false;
                    isScrolling_ = false;
                }
                break;
            }

            case AMOTION_EVENT_ACTION_MOVE: {
                if (isPinching_ && pointerCount >= 2) {
                    // Handle pinch zoom
                    auto &pointer1 = motionEvent.pointers[0];
                    auto &pointer2 = motionEvent.pointers[1];
                    
                    float x1 = GameActivityPointerAxes_getX(&pointer1);
                    float y1 = GameActivityPointerAxes_getY(&pointer1);
                    float x2 = GameActivityPointerAxes_getX(&pointer2);
                    float y2 = GameActivityPointerAxes_getY(&pointer2);
                    
                    convertScreenToWorld(x1, y1, touch1_.x, touch1_.y);
                    convertScreenToWorld(x2, y2, touch2_.x, touch2_.y);
                    
                    float currentDistance = calculateDistance(touch1_.x, touch1_.y, touch2_.x, touch2_.y);
                    
                    if (lastPinchDistance_ > 0.0f) {
                        float scale = currentDistance / lastPinchDistance_;
                        float newZoom = zoomLevel_ * scale;
                        
                        // Clamp zoom level
                        if (newZoom >= minZoom_ && newZoom <= maxZoom_) {
                            zoomLevel_ = newZoom;
                            shaderNeedsNewProjectionMatrix_ = true;
                            
                            aout << "Zoom: " << zoomLevel_ << " (scale=" << scale << ", dist=" << currentDistance << ")" << std::endl;
                        }
                    }
                    
                    lastPinchDistance_ = currentDistance;
                    
                } else if (isScrolling_ && !isPinching_) {
                    // Handle single finger scroll
                    auto &pointer = motionEvent.pointers[0];
                    auto x = GameActivityPointerAxes_getX(&pointer);
                    auto y = GameActivityPointerAxes_getY(&pointer);
                    
                    float worldX, worldY;
                    convertScreenToWorld(x, y, worldX, worldY);
                    
                    // Calculate the delta movement
                    float deltaX = worldX - lastTouchX_;
                    float deltaY = worldY - lastTouchY_;
                    
                    // Update scroll position
                    scrollX_ += deltaX;
                    scrollY_ += deltaY;
                    
                    // Update last touch position
                    lastTouchX_ = worldX;
                    lastTouchY_ = worldY;
                    
                    aout << "Scroll: (" << scrollX_ << ", " << scrollY_ << ")" << std::endl;
                }
                break;
            }
            
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

bool Renderer::decodePNGToTexture() {
    if (tankImageData_.empty()) {
        aout << "No tank image data to decode" << std::endl;
        return false;
    }
    
    aout << "Decoding PNG data using BitmapFactory, size: " << tankImageData_.size() << " bytes" << std::endl;
    
    JNIEnv* env = getJNIEnv();
    if (!env) {
        aout << "Failed to get JNI environment" << std::endl;
        return false;
    }
    
    // Create byte array from image data
    jbyteArray byteArray = env->NewByteArray(tankImageData_.size());
    if (!byteArray) {
        aout << "Failed to create byte array" << std::endl;
        return false;
    }
    
    env->SetByteArrayRegion(byteArray, 0, tankImageData_.size(), 
                           reinterpret_cast<const jbyte*>(tankImageData_.data()));
    
    // Get BitmapFactory class and decodeByteArray method
    jclass bitmapFactoryClass = env->FindClass("android/graphics/BitmapFactory");
    if (!bitmapFactoryClass) {
        aout << "Failed to find BitmapFactory class" << std::endl;
        env->DeleteLocalRef(byteArray);
        return false;
    }
    
    jmethodID decodeByteArrayMethod = env->GetStaticMethodID(bitmapFactoryClass, 
                                                           "decodeByteArray", 
                                                           "([BII)Landroid/graphics/Bitmap;");
    if (!decodeByteArrayMethod) {
        aout << "Failed to find decodeByteArray method" << std::endl;
        env->DeleteLocalRef(byteArray);
        env->DeleteLocalRef(bitmapFactoryClass);
        return false;
    }
    
    // Decode the image
    jobject bitmap = env->CallStaticObjectMethod(bitmapFactoryClass, decodeByteArrayMethod, 
                                                byteArray, 0, tankImageData_.size());
    
    if (!bitmap) {
        aout << "Failed to decode bitmap" << std::endl;
        env->DeleteLocalRef(byteArray);
        env->DeleteLocalRef(bitmapFactoryClass);
        return false;
    }
    
    // Get bitmap info
    AndroidBitmapInfo bitmapInfo;
    int result = AndroidBitmap_getInfo(env, bitmap, &bitmapInfo);
    if (result != ANDROID_BITMAP_RESULT_SUCCESS) {
        aout << "Failed to get bitmap info, result: " << result << std::endl;
        env->DeleteLocalRef(bitmap);
        env->DeleteLocalRef(byteArray);
        env->DeleteLocalRef(bitmapFactoryClass);
        return false;
    }
    
    tankTextureWidth_ = bitmapInfo.width;
    tankTextureHeight_ = bitmapInfo.height;
    
    aout << "Tank image dimensions: " << tankTextureWidth_ << "x" << tankTextureHeight_ << std::endl;
    
    // Lock bitmap pixels
    void* bitmapPixels;
    result = AndroidBitmap_lockPixels(env, bitmap, &bitmapPixels);
    if (result != ANDROID_BITMAP_RESULT_SUCCESS) {
        aout << "Failed to lock bitmap pixels, result: " << result << std::endl;
        env->DeleteLocalRef(bitmap);
        env->DeleteLocalRef(byteArray);
        env->DeleteLocalRef(bitmapFactoryClass);
        return false;
    }
    
    // Create OpenGL texture
    glGenTextures(1, &tankTextureId_);
    glBindTexture(GL_TEXTURE_2D, tankTextureId_);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Upload texture data - BitmapFactory typically returns RGBA_8888
    GLenum format = (bitmapInfo.format == ANDROID_BITMAP_FORMAT_RGBA_8888) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, tankTextureWidth_, tankTextureHeight_, 0, 
                format, GL_UNSIGNED_BYTE, bitmapPixels);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Unlock bitmap pixels
    AndroidBitmap_unlockPixels(env, bitmap);
    
    // Clean up JNI references
    env->DeleteLocalRef(bitmap);
    env->DeleteLocalRef(byteArray);
    env->DeleteLocalRef(bitmapFactoryClass);
    
    tankTextureLoaded_ = true;
    aout << "Tank texture created successfully using BitmapFactory, ID: " << tankTextureId_ << std::endl;
    
    return true;
}

JNIEnv* Renderer::getJNIEnv() {
    JavaVM* jvm = app_->activity->vm;
    JNIEnv* env = nullptr;
    
    jint result = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
        // Thread not attached, attach it
        result = jvm->AttachCurrentThread(&env, nullptr);
        if (result != JNI_OK) {
            aout << "Failed to attach current thread to JVM" << std::endl;
            return nullptr;
        }
    } else if (result != JNI_OK) {
        aout << "Failed to get JNI environment" << std::endl;
        return nullptr;
    }
    
    return env;
}

void Renderer::checkTankSelection(float worldX, float worldY) {
    if (!mapDataLoaded_) {
        return;
    }
    
    // Input coordinates are already in world space, just account for scroll
    float adjustedWorldX = worldX - scrollX_;
    float adjustedWorldY = worldY - scrollY_;
    
    // Grid parameters (same as in createColoredGrid)
    const int gridSize = std::max(mapData_.width, mapData_.height);
    const float gridSpacing = 0.4f;
    const float gridExtent = gridSize * gridSpacing * 0.5f;
    
    // Convert world coordinates to grid coordinates
    // Reverse the formula used in createColoredGrid:
    // cellX = -gridExtent + (x + 0.5f) * gridSpacing  =>  x = (cellX + gridExtent) / gridSpacing - 0.5
    // cellY = gridExtent - (y + 0.5f) * gridSpacing   =>  y = (gridExtent - cellY) / gridSpacing - 0.5
    float gridX = (adjustedWorldX + gridExtent) / gridSpacing - 0.5f;
    float gridY = (gridExtent - adjustedWorldY) / gridSpacing - 0.5f;
    
    // Convert to integer grid coordinates with proper rounding
    int gx = (int)round(gridX);
    int gy = (int)round(gridY);
    
    aout << "Touch conversion: world(" << worldX << ", " << worldY << ") -> adjusted(" << adjustedWorldX << ", " << adjustedWorldY << ") -> grid_float(" << gridX << ", " << gridY << ") -> grid_int(" << gx << ", " << gy << ")" << std::endl;
    aout << "Grid extent: " << gridExtent << ", Grid spacing: " << gridSpacing << ", Scroll: (" << scrollX_ << ", " << scrollY_ << "), Zoom: " << zoomLevel_ << std::endl;
    
    // Debug: show expected cell center for this grid position
    if (gx >= 0 && gx < mapData_.width && gy >= 0 && gy < mapData_.height) {
        float expectedCellX = -gridExtent + (gx + 0.5f) * gridSpacing;
        float expectedCellY = gridExtent - (gy + 0.5f) * gridSpacing;
        aout << "Expected cell center for grid(" << gx << "," << gy << "): world(" << expectedCellX << ", " << expectedCellY << ")" << std::endl;
    }
    
    // Check if coordinates are within grid bounds
    if (gx >= 0 && gx < mapData_.width && gy >= 0 && gy < mapData_.height) {
        char cellType = mapData_.data[gy * mapData_.width + gx];
        
        aout << "=== GRID CELL ANALYSIS ===" << std::endl;
        aout << "Grid position: (" << gx << ", " << gy << ")" << std::endl;
        aout << "Cell type: '" << cellType << "' (ASCII: " << (int)cellType << ")" << std::endl;
        aout << "Map dimensions: " << mapData_.width << "x" << mapData_.height << std::endl;
        aout << "Array index: " << (gy * mapData_.width + gx) << " of " << mapData_.data.size() << std::endl;
        
        if (cellType == 'x' || cellType == 'X') {
            // Tank found! Select it
            selectedTankX_ = gx;
            selectedTankY_ = gy;
            hasTankSelected_ = true;
            
            aout << "*** TANK SELECTED! ***" << std::endl;
            aout << "Selected tank at grid position (" << gx << ", " << gy << ")" << std::endl;
            aout << "Previous selection: " << (hasTankSelected_ ? "Yes" : "No") << std::endl;
            
            // Create highlight overlay for selected tank
            createHighlightOverlay();
            
            // Send highlight request to server
            sendHighlightRequest(gx, gy);
        } else {
            // No tank at this position, clear selection
            aout << "No tank found - clearing selection" << std::endl;
            aout << "Expected 'x' or 'X', got '" << cellType << "'" << std::endl;
            hasTankSelected_ = false;
            highlightModels_.clear(); // Clear highlight overlay
            aout << "No tank at grid position (" << gx << ", " << gy << "), cell type: '" << cellType << "'" << std::endl;
        }
    } else {
        // Outside grid bounds, clear selection
        aout << "=== OUT OF BOUNDS ===" << std::endl;
        aout << "Grid position: (" << gx << ", " << gy << ")" << std::endl;
        aout << "Grid bounds: 0-" << (mapData_.width-1) << " x 0-" << (mapData_.height-1) << std::endl;
        hasTankSelected_ = false;
        highlightModels_.clear(); // Clear highlight overlay
        aout << "Touch outside grid bounds" << std::endl;
    }
}

void Renderer::createHighlightOverlay() {
    aout << "=== CREATING HIGHLIGHT OVERLAY ===" << std::endl;
    aout << "Has tank selected: " << (hasTankSelected_ ? "YES" : "NO") << std::endl;
    
    if (!hasTankSelected_) {
        highlightModels_.clear();
        aout << "No tank selected - clearing highlight models" << std::endl;
        return;
    }
    
    aout << "Selected tank position: (" << selectedTankX_ << ", " << selectedTankY_ << ")" << std::endl;
    
    // Grid parameters (same as in createColoredGrid)
    const int gridSize = std::max(mapData_.width, mapData_.height);
    const float gridSpacing = 0.4f;
    const float gridExtent = gridSize * gridSpacing * 0.5f;
    const float cellSize = gridSpacing * 0.8f;
    
    // Calculate position of selected tank
    float cellX = -gridExtent + (selectedTankX_ + 0.5f) * gridSpacing;
    float cellY = gridExtent - (selectedTankY_ + 0.5f) * gridSpacing;
    
    // Create red highlight overlay (slightly larger than the tank)
    float highlightSize = cellSize * 1.1f; // 10% larger for visible border
    Vector3 highlightColor = {1.0f, 0.0f, 0.0f}; // Red
    
    std::vector<Vertex> highlightVertices;
    std::vector<Index> highlightIndices;
    
    // Create highlight border (outline only)
    Index baseIndex = highlightVertices.size();
    
    // Add vertices for highlight border
    highlightVertices.emplace_back(Vector3{cellX - highlightSize/2, cellY + highlightSize/2, 0.01f}, highlightColor); // Top-left (slightly above)
    highlightVertices.emplace_back(Vector3{cellX + highlightSize/2, cellY + highlightSize/2, 0.01f}, highlightColor); // Top-right
    highlightVertices.emplace_back(Vector3{cellX + highlightSize/2, cellY - highlightSize/2, 0.01f}, highlightColor); // Bottom-right
    highlightVertices.emplace_back(Vector3{cellX - highlightSize/2, cellY - highlightSize/2, 0.01f}, highlightColor); // Bottom-left
    
    // Add indices for highlight outline (thick lines)
    highlightIndices.push_back(baseIndex);     highlightIndices.push_back(baseIndex + 1); // Top
    highlightIndices.push_back(baseIndex + 1); highlightIndices.push_back(baseIndex + 2); // Right  
    highlightIndices.push_back(baseIndex + 2); highlightIndices.push_back(baseIndex + 3); // Bottom
    highlightIndices.push_back(baseIndex + 3); highlightIndices.push_back(baseIndex);     // Left
    
    // Clear and create new highlight model
    highlightModels_.clear();
    if (!highlightVertices.empty()) {
        highlightModels_.emplace_back(highlightVertices, highlightIndices);
        aout << "Created highlight overlay for tank at (" << selectedTankX_ << ", " << selectedTankY_ << ")" << std::endl;
        aout << "Highlight model: " << highlightVertices.size() << " vertices, " << highlightIndices.size() << " indices" << std::endl;
        aout << "Highlight position: (" << cellX << ", " << cellY << "), size: " << highlightSize << std::endl;
    } else {
        aout << "ERROR: No highlight vertices created!" << std::endl;
    }
    aout << "=== END HIGHLIGHT CREATION ===" << std::endl;
}

float Renderer::calculateDistance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

void Renderer::updateProjectionMatrixWithZoom() {
    // Clear the projection matrix
    memset(projectionMatrix_, 0, sizeof(projectionMatrix_));

    // Apply zoom to the projection matrix by scaling the half height
    float zoomedHalfHeight = kProjectionHalfHeight / zoomLevel_;
    
    // Build an orthographic projection matrix for 2d rendering with zoom
    Utility::buildOrthographicMatrix(
            projectionMatrix_,
            zoomedHalfHeight,
            float(width_) / height_,
            kProjectionNearPlane,
            kProjectionFarPlane);

    // Send the matrix to all shaders
    shader_->setProjectionMatrix(projectionMatrix_);
    if (triangleShader_) {
        triangleShader_->setProjectionMatrix(projectionMatrix_);
    }
    if (textureShader_) {
        textureShader_->setProjectionMatrix(projectionMatrix_);
    }
    
    aout << "Updated projection matrix with zoom level: " << zoomLevel_ << std::endl;
}

void Renderer::convertScreenToWorld(float screenX, float screenY, float& worldX, float& worldY) {
    // Convert screen coordinates to world coordinates with zoom support
    float aspect = float(width_) / float(height_);
    float zoomedHalfHeight = kProjectionHalfHeight / zoomLevel_;
    float zoomedHalfWidth = zoomedHalfHeight * aspect;
    
    float normalizedX = ((screenX / float(width_)) * 2.0f - 1.0f) * zoomedHalfWidth;
    float normalizedY = -(((screenY / float(height_)) * 2.0f - 1.0f) * zoomedHalfHeight);
    
    worldX = normalizedX;
    worldY = normalizedY;
}

void Renderer::sendHighlightRequest(int gridX, int gridY) {
    aout << "Sending highlight request for grid position (" << gridX << ", " << gridY << ")" << std::endl;
    
    // Create JSON payload
    std::string jsonPayload = "{\n";
    jsonPayload += "    \"x\": " + std::to_string(gridX) + ",\n";
    jsonPayload += "    \"y\": " + std::to_string(gridY) + ",\n";
    jsonPayload += "    \"value\": \"XH\"\n";
    jsonPayload += "}";
    
    aout << "JSON payload: " << jsonPayload << std::endl;
    
    // Send POST request
    std::string response;
    bool success = NetworkDownloader::postJSON("http://nasmo2.myqnapcloud.com:8585/tanks/index.php", jsonPayload, response);
    
    if (success) {
        aout << "Highlight request sent successfully!" << std::endl;
        aout << "Server response: " << response << std::endl;
    } else {
        aout << "Failed to send highlight request" << std::endl;
    }
}