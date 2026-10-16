#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace colorfinder {
namespace fs = std::filesystem;

inline bool hasImageExtension(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return extension == ".bmp" || extension == ".gif" || extension == ".jpeg"
        || extension == ".jpg" || extension == ".png" || extension == ".tif"
        || extension == ".tiff" || extension == ".webp";
}

inline bool hasNumericSuffix(const fs::path& path) {
    const std::string name = path.filename().string();
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 == name.size()) {
        return false;
    }
    return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(dot + 1), name.end(),
                       [](unsigned char character) {
                           return std::isdigit(character) != 0;
                       });
}

inline bool findTextureFolder(const fs::path& modelRoot, fs::path& textureFolder) {
    int bestImageCount = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(modelRoot)) {
        if (!entry.is_directory() || !hasNumericSuffix(entry.path())) {
            continue;
        }
        int imageCount = 0;
        for (const fs::directory_entry& texture : fs::directory_iterator(entry.path())) {
            if (texture.is_regular_file() && hasImageExtension(texture.path())) {
                ++imageCount;
            }
        }
        if (imageCount > bestImageCount) {
            bestImageCount = imageCount;
            textureFolder = entry.path();
        }
    }
    return bestImageCount > 0;
}

inline bool findMoc3File(const fs::path& modelRoot, fs::path& moc3File) {
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(modelRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".moc3") {
            moc3File = entry.path();
            return true;
        }
    }
    return false;
}

} // namespace colorfinder
