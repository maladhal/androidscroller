#ifndef SCROLLER_NETWORKDOWNLOADER_H
#define SCROLLER_NETWORKDOWNLOADER_H

#include <string>
#include <vector>
#include <functional>

class NetworkDownloader {
public:
    struct MapData {
        std::vector<char> data;
        int width;
        int height;
    };

    static bool downloadCSV(const std::string& url, MapData& mapData);
    static bool downloadImage(const std::string& url, std::vector<uint8_t>& imageData);

private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    static size_t WriteImageCallback(void* contents, size_t size, size_t nmemb, std::vector<uint8_t>* userp);
    static bool parseCSVData(const char* csvData, MapData& mapData);
};

#endif //SCROLLER_NETWORKDOWNLOADER_H