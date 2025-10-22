#include "NetworkDownloader.h"
#include "AndroidOut.h"
#include <jni.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <sstream>

extern struct android_app* g_app; // Global app pointer from main.cpp

size_t NetworkDownloader::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t realsize = size * nmemb;
    userp->append((char*)contents, realsize);
    return realsize;
}

size_t NetworkDownloader::WriteImageCallback(void* contents, size_t size, size_t nmemb, std::vector<uint8_t>* userp) {
    size_t realsize = size * nmemb;
    uint8_t* data = (uint8_t*)contents;
    userp->insert(userp->end(), data, data + realsize);
    return realsize;
}

bool NetworkDownloader::downloadCSV(const std::string& url, MapData& mapData) {
    aout << "NetworkDownloader::downloadCSV called with URL: " << url << std::endl;
    
    if (!g_app) {
        aout << "g_app is null!" << std::endl;
        return false;
    }
    
    if (!g_app->activity) {
        aout << "g_app->activity is null!" << std::endl;
        return false;
    }
    
    aout << "App and activity available, trying JNI..." << std::endl;

    JNIEnv* env;
    JavaVM* vm = g_app->activity->vm;
    
    if (!vm) {
        aout << "JavaVM is null!" << std::endl;
        return false;
    }
    
    // Try a simpler approach first
    jint result = vm->AttachCurrentThread(&env, nullptr);
    if (result != JNI_OK) {
        aout << "Failed to attach to Java VM: " << result << std::endl;
        return false;
    }
    
    aout << "Successfully attached to JVM" << std::endl;

    // Get the current activity object to access its class loader
    jobject activityObj = g_app->activity->javaGameActivity;
    if (!activityObj) {
        aout << "Activity object is null!" << std::endl;
        vm->DetachCurrentThread();
        return false;
    }

    // Get the activity's class
    jclass activityClass = env->GetObjectClass(activityObj);
    if (!activityClass) {
        aout << "Failed to get activity class" << std::endl;
        vm->DetachCurrentThread();
        return false;
    }

    // Get the class loader from the activity
    jmethodID getClassLoaderMethod = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getClassLoaderMethod) {
        aout << "Failed to get getClassLoader method" << std::endl;
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }

    jobject classLoader = env->CallObjectMethod(activityObj, getClassLoaderMethod);
    if (!classLoader) {
        aout << "Failed to get class loader" << std::endl;
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }

    // Get the loadClass method
    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassMethod = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClassMethod) {
        aout << "Failed to get loadClass method" << std::endl;
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }

    // Load the NetworkHelper class
    jstring className = env->NewStringUTF("com.example.scroller.NetworkHelper");
    jclass networkHelperClass = (jclass)env->CallObjectMethod(classLoader, loadClassMethod, className);
    
    if (!networkHelperClass || env->ExceptionCheck()) {
        aout << "Failed to load NetworkHelper class using ClassLoader" << std::endl;
        env->ExceptionDescribe();
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }
    
    aout << "Found NetworkHelper class!" << std::endl;
    
    // Get the downloadText method
    jmethodID downloadTextMethod = env->GetStaticMethodID(networkHelperClass, "downloadText", "(Ljava/lang/String;)Ljava/lang/String;");
    if (!downloadTextMethod) {
        aout << "Failed to find downloadText method" << std::endl;
        env->ExceptionDescribe();
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }
    
    // Create jstring for URL
    jstring jUrl = env->NewStringUTF(url.c_str());
    
    // Call the method
    jstring result_str = (jstring)env->CallStaticObjectMethod(networkHelperClass, downloadTextMethod, jUrl);
    
    if (!result_str || env->ExceptionCheck()) {
        aout << "Failed to download CSV or exception occurred" << std::endl;
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(jUrl);
        vm->DetachCurrentThread();
        return false;
    }
    
    // Convert result to string
    const char* csvData = env->GetStringUTFChars(result_str, nullptr);
    if (!csvData) {
        aout << "Failed to get string data" << std::endl;
        env->DeleteLocalRef(jUrl);
        env->DeleteLocalRef(result_str);
        vm->DetachCurrentThread();
        return false;
    }
    
    aout << "Downloaded CSV data, size: " << strlen(csvData) << std::endl;
    
    // Parse the CSV data
    bool parseResult = parseCSVData(csvData, mapData);
    
    // Cleanup
    env->ReleaseStringUTFChars(result_str, csvData);
    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(result_str);
    env->DeleteLocalRef(className);
    env->DeleteLocalRef(classLoader);
    env->DeleteLocalRef(classLoaderClass);
    env->DeleteLocalRef(activityClass);
    vm->DetachCurrentThread();
    
    return parseResult;
}

bool NetworkDownloader::downloadImage(const std::string& url, std::vector<uint8_t>& imageData) {
    aout << "NetworkDownloader::downloadImage called with URL: " << url << std::endl;
    
    if (!g_app || !g_app->activity) {
        aout << "No app activity available for JNI calls" << std::endl;
        return false;
    }

    aout << "App and activity available, trying JNI..." << std::endl;

    JNIEnv* env;
    JavaVM* vm = g_app->activity->vm;
    jint result = vm->AttachCurrentThread(&env, nullptr);
    if (result != JNI_OK) {
        aout << "Failed to attach to Java VM: " << result << std::endl;
        return false;
    }

    aout << "Successfully attached to JVM" << std::endl;

    // Get the activity class and context
    jobject activityObject = g_app->activity->javaGameActivity;
    jclass activityClass = env->GetObjectClass(activityObject);
    
    // Get the class loader from the activity
    jmethodID getClassLoaderMethod = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getClassLoaderMethod) {
        aout << "Failed to get getClassLoader method" << std::endl;
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }

    jobject classLoader = env->CallObjectMethod(activityObject, getClassLoaderMethod);
    if (!classLoader) {
        aout << "Failed to get class loader" << std::endl;
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }

    // Get the loadClass method
    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClassMethod = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClassMethod) {
        aout << "Failed to get loadClass method" << std::endl;
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }

    // Load the NetworkHelper class
    jstring className = env->NewStringUTF("com.example.scroller.NetworkHelper");
    jclass networkHelperClass = (jclass)env->CallObjectMethod(classLoader, loadClassMethod, className);
    
    if (!networkHelperClass || env->ExceptionCheck()) {
        aout << "Failed to load NetworkHelper class using ClassLoader" << std::endl;
        env->ExceptionDescribe();
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }
    
    aout << "Found NetworkHelper class!" << std::endl;
    
    // Get the downloadImageData method
    jmethodID downloadImageDataMethod = env->GetStaticMethodID(networkHelperClass, "downloadImageData", "(Ljava/lang/String;)[B");
    if (!downloadImageDataMethod) {
        aout << "Failed to find downloadImageData method" << std::endl;
        env->ExceptionDescribe();
        env->ExceptionClear();
        vm->DetachCurrentThread();
        return false;
    }

    // Create Java string for URL
    jstring jUrl = env->NewStringUTF(url.c_str());
    if (!jUrl) {
        aout << "Failed to create Java string for URL" << std::endl;
        vm->DetachCurrentThread();
        return false;
    }

    // Call the download method
    jbyteArray result_array = (jbyteArray)env->CallStaticObjectMethod(networkHelperClass, downloadImageDataMethod, jUrl);
    
    if (env->ExceptionCheck()) {
        aout << "Exception occurred during image download" << std::endl;
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->DeleteLocalRef(jUrl);
        env->DeleteLocalRef(className);
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(classLoaderClass);
        env->DeleteLocalRef(activityClass);
        vm->DetachCurrentThread();
        return false;
    }
    
    if (!result_array) {
        aout << "Image download failed - null result" << std::endl;
        env->DeleteLocalRef(jUrl);
        env->DeleteLocalRef(className);
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(classLoaderClass);
        env->DeleteLocalRef(activityClass);
        vm->DetachCurrentThread();
        return false;
    }

    // Convert byte array to vector
    jsize arrayLength = env->GetArrayLength(result_array);
    jbyte* arrayPtr = env->GetByteArrayElements(result_array, nullptr);
    
    imageData.clear();
    imageData.resize(arrayLength);
    memcpy(imageData.data(), arrayPtr, arrayLength);
    
    env->ReleaseByteArrayElements(result_array, arrayPtr, JNI_ABORT);
    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(result_array);
    env->DeleteLocalRef(className);
    env->DeleteLocalRef(classLoader);
    env->DeleteLocalRef(classLoaderClass);
    env->DeleteLocalRef(activityClass);
    vm->DetachCurrentThread();

    aout << "Successfully downloaded image data, size: " << imageData.size() << " bytes" << std::endl;
    return true;
}

bool NetworkDownloader::parseCSVData(const char* csvData, MapData& mapData) {
    aout << "Parsing CSV data..." << std::endl;
    
    std::vector<std::vector<char>> grid;
    std::istringstream stream(csvData);
    std::string line;
    int maxWidth = 0;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        std::vector<char> row;
        std::istringstream lineStream(line);
        std::string cell;
        
        while (std::getline(lineStream, cell, ',')) {
            // Remove whitespace
            cell.erase(0, cell.find_first_not_of(" \t\r\n"));
            cell.erase(cell.find_last_not_of(" \t\r\n") + 1);
            
            if (!cell.empty()) {
                row.push_back(cell[0]);
            } else {
                row.push_back(' '); // Empty cell
            }
        }
        
        if (!row.empty()) {
            grid.push_back(row);
            maxWidth = std::max(maxWidth, (int)row.size());
        }
    }

    // Normalize grid (make all rows same width)
    for (auto& row : grid) {
        while (row.size() < maxWidth) {
            row.push_back(' ');
        }
    }

    // Convert to MapData
    mapData.width = maxWidth;
    mapData.height = grid.size();
    mapData.data.resize(mapData.width * mapData.height);

    for (int y = 0; y < mapData.height; y++) {
        for (int x = 0; x < mapData.width; x++) {
            mapData.data[y * mapData.width + x] = grid[y][x];
        }
    }

    aout << "Successfully parsed CSV data: " << mapData.width << "x" << mapData.height << std::endl;
    return true;
}