#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <memory>

#include "Model.h"
#include "Shader.h"
#include "TextureShader.h"
#include "NetworkDownloader.h"
#include <jni.h>

struct android_app;

class Renderer {
public:
    /*!
     * @param pApp the android_app this Renderer belongs to, needed to configure GL
     */
    inline Renderer(android_app *pApp) :
            app_(pApp),
            display_(EGL_NO_DISPLAY),
            surface_(EGL_NO_SURFACE),
            context_(EGL_NO_CONTEXT),
            width_(0),
            height_(0),
            shaderNeedsNewProjectionMatrix_(true),
            mapDataLoaded_(false),
            scrollX_(0.0f),
            scrollY_(0.0f),
            lastTouchX_(0.0f),
            lastTouchY_(0.0f),
            isScrolling_(false),
            zoomLevel_(1.0f),
            minZoom_(0.5f),
            maxZoom_(3.0f),
            isPinching_(false),
            lastPinchDistance_(0.0f),
            selectedTankX_(-1),
            selectedTankY_(-1),
            hasTankSelected_(false) {
        touch1_.active = false;
        touch2_.active = false;
        initRenderer();
    }

    virtual ~Renderer();

    /*!
     * Handles input from the android_app.
     *
     * Note: this will clear the input queue
     */
    void handleInput();

    /*!
     * Renders all the models in the renderer
     */
    void render();

private:
    /*!
     * Performs necessary OpenGL initialization. Customize this if you want to change your EGL
     * context or application-wide settings.
     */
    void initRenderer();

    /*!
     * @brief we have to check every frame to see if the framebuffer has changed in size. If it has,
     * update the viewport accordingly
     */
    void updateRenderArea();

    /*!
     * Creates the models for this sample. You'd likely load a scene configuration from a file or
     * use some other setup logic in your full game.
     */
    void createModels();
    
    /*!
     * Downloads and processes map data
     */
    void downloadMapData();
    
    /*!
     * Creates fallback test map data when network download fails
     */
    void createFallbackMapData();
    
    /*!
     * Creates colored grid based on map data
     */
    void createColoredGrid();
    
    /*!
     * Decodes PNG image data and creates OpenGL texture using BitmapFactory (API 24+ compatible)
     */
    bool decodePNGToTexture();
    
    /*!
     * Helper function to get JNI environment
     */
    JNIEnv* getJNIEnv();
    
    /*!
     * Converts screen coordinates to grid coordinates and checks for tank selection
     */
    void checkTankSelection(float worldX, float worldY);
    
    /*!
     * Creates highlight overlay for selected tank
     */
    void createHighlightOverlay();
    
    /*!
     * Helper methods for zoom functionality
     */
    float calculateDistance(float x1, float y1, float x2, float y2);
    void updateProjectionMatrixWithZoom();
    void convertScreenToWorld(float screenX, float screenY, float& worldX, float& worldY);
    
    /*!
     * Sends highlight request to server when tank is selected
     */
    void sendHighlightRequest(int gridX, int gridY);
    
    /*!
     * Gets list of adjacent empty coordinates for a given position
     * Returns coordinates of all adjacent cells (up, down, left, right, and diagonals)
     * that are within bounds and contain empty/space values
     */
    std::vector<std::pair<int, int>> getAdjacentEmptyCoordinates(int centerX, int centerY) const;

    android_app *app_;
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    EGLint width_;
    EGLint height_;

    bool shaderNeedsNewProjectionMatrix_;

    std::unique_ptr<Shader> shader_;
    std::unique_ptr<Shader> triangleShader_;
    std::unique_ptr<TextureShader> textureShader_;
    std::vector<Model> models_;
    std::vector<Model> triangleModels_;
    std::vector<TexturedModel> texturedModels_;
    std::vector<Model> highlightModels_;
    
    // Map data
    NetworkDownloader::MapData mapData_;
    bool mapDataLoaded_;
    std::vector<uint8_t> tankImageData_;
    
    // Tank texture data
    GLuint tankTextureId_;
    int tankTextureWidth_;
    int tankTextureHeight_;
    bool tankTextureLoaded_;
    
    // Scrolling variables
    float scrollX_;
    float scrollY_;
    float lastTouchX_;
    float lastTouchY_;
    bool isScrolling_;
    
    // Zoom variables
    float zoomLevel_;
    float minZoom_;
    float maxZoom_;
    
    // Multi-touch tracking
    struct TouchPoint {
        float x, y;
        bool active;
    };
    TouchPoint touch1_;
    TouchPoint touch2_;
    bool isPinching_;
    float lastPinchDistance_;
    
    // Matrix storage for sharing between shaders
    float projectionMatrix_[16];
    float modelMatrix_[16];
    
    // Selection tracking
    int selectedTankX_;
    int selectedTankY_;
    bool hasTankSelected_;
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H