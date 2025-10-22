#include <android/bitmap.h>
#include <jni.h>
#include "TextureAsset.h"
#include "AndroidOut.h"
#include "Utility.h"

std::shared_ptr<TextureAsset>
TextureAsset::loadAsset(AAssetManager *assetManager, const std::string &assetPath) {
    // TODO: Implement BitmapFactory-based asset loading for API 24+ compatibility
    // For now, return nullptr as this function is not currently used
    aout << "TextureAsset::loadAsset called but not implemented for API 24 compatibility" << std::endl;
    return nullptr;
}

TextureAsset::~TextureAsset() {
    // return texture resources
    glDeleteTextures(1, &textureID_);
    textureID_ = 0;
}