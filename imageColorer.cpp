#define NOMINMAX
#include <opencv2/core/persistence.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include <windows.h>
#include "model_paths.h"

namespace fs = std::filesystem;

const std::set<std::string> imageExtensions = {
    ".bmp", ".gif", ".jpeg", ".jpg", ".png", ".tif", ".tiff", ".webp"
};
constexpr char colorMapFileName[] = "colorfinder_colors.json";
constexpr int maxPaletteColors = 16 * 16 * 16;

bool getModelRoot(fs::path& folder) {
    wchar_t value[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"COLORFINDER_IMAGE_DIR", value, static_cast<DWORD>(std::size(value))
    );
    if (length == 0 || length >= std::size(value)) {
        return false;
    }

    folder = fs::path(value);
    return fs::is_directory(folder);
}

bool isImage(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return fs::is_regular_file(path) && imageExtensions.count(extension) != 0;
}

cv::Mat loadRgba(const fs::path& path) {
    cv::Mat image = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        return {};
    }

    if (image.channels() == 4) {
        return image;
    }
    if (image.channels() == 3) {
        cv::Mat rgba;
        cv::cvtColor(image, rgba, cv::COLOR_BGR2BGRA);
        return rgba;
    }

    cv::Mat rgba;
    cv::cvtColor(image, rgba, cv::COLOR_GRAY2BGRA);
    for (int row = 0; row < rgba.rows; ++row) {
        for (int column = 0; column < rgba.cols; ++column) {
            rgba.at<cv::Vec4b>(row, column)[3] = 255;
        }
    }
    return rgba;
}

cv::Mat groupedTextureMask(const cv::Mat& rgba) {
    cv::Mat alpha(rgba.rows, rgba.cols, CV_8UC1);
    for (int row = 0; row < rgba.rows; ++row) {
        for (int column = 0; column < rgba.cols; ++column) {
            alpha.at<unsigned char>(row, column) =
                rgba.at<cv::Vec4b>(row, column)[3] > 0 ? 255 : 0;
        }
    }

    const cv::Mat groupingKernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE,
        cv::Size(5, 5)
    );
    cv::Mat grouped;
    cv::dilate(alpha, grouped, groupingKernel);
    return grouped;
}

int countComponents(const cv::Mat& rgba) {
    cv::Mat labels;
    return cv::connectedComponents(groupedTextureMask(rgba), labels, 8) - 1;
}

std::vector<cv::Vec3b> buildPalette(int totalComponents) {
    std::vector<cv::Vec3b> candidates;
    for (int blue = 8; blue <= 248; blue += 16) {
        for (int green = 8; green <= 248; green += 16) {
            for (int red = 8; red <= 248; red += 16) {
                candidates.emplace_back(
                    static_cast<unsigned char>(blue),
                    static_cast<unsigned char>(green),
                    static_cast<unsigned char>(red)
                );
            }
        }
    }

    std::vector<cv::Vec3b> palette;
    std::vector<int> bestDistances(candidates.size(), INT_MAX);
    for (int component = 0; component < totalComponents; ++component) {
        std::size_t bestCandidate = 0;
        if (component > 0) {
            for (std::size_t candidate = 1; candidate < candidates.size(); ++candidate) {
                if (bestDistances[candidate] > bestDistances[bestCandidate]) {
                    bestCandidate = candidate;
                }
            }
        }

        const cv::Vec3b selected = candidates[bestCandidate];
        palette.push_back(selected);
        for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate) {
            const int blueDifference = selected[0] - candidates[candidate][0];
            const int greenDifference = selected[1] - candidates[candidate][1];
            const int redDifference = selected[2] - candidates[candidate][2];
            const int distance = blueDifference * blueDifference
                + greenDifference * greenDifference
                + redDifference * redDifference;
            bestDistances[candidate] = std::min(bestDistances[candidate], distance);
        }
    }
    return palette;
}

cv::Mat colorImage(const cv::Mat& rgba, int offset, const std::vector<cv::Vec3b>& palette) {
    cv::Mat labels;
    const int componentCount = cv::connectedComponents(
        groupedTextureMask(rgba), labels, 8
    ) - 1;
    cv::Mat output = rgba.clone();
    for (int label = 1; label <= componentCount; ++label) {
        const cv::Vec3b color = palette[offset + label - 1];
        for (int row = 0; row < output.rows; ++row) {
            for (int column = 0; column < output.cols; ++column) {
                if (labels.at<int>(row, column) == label
                    && rgba.at<cv::Vec4b>(row, column)[3] > 0) {
                    cv::Vec4b& pixel = output.at<cv::Vec4b>(row, column);
                    pixel[0] = color[0];
                    pixel[1] = color[1];
                    pixel[2] = color[2];
                    pixel[3] = 255;
                }
            }
        }
    }
    return output;
}

bool removeMoc3Effects(const fs::path& moc3File) {
    std::ifstream input(moc3File, std::ios::binary);
    std::vector<unsigned char> data(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()
    );
    if (data.size() < 0x40 + 160 * sizeof(std::uint32_t)
        || std::string(data.begin(), data.begin() + 4) != "MOC3") {
        std::cerr << "The moc3 file has an unsupported or invalid format.\n";
        return false;
    }

    auto readU32 = [&data](std::size_t offset) -> std::uint32_t {
        std::uint32_t value = 0;
        std::memcpy(&value, data.data() + offset, sizeof(value));
        return value;
    };
    auto writeFloat = [&data](std::size_t offset, float value) {
        std::memcpy(data.data() + offset, &value, sizeof(value));
    };

    std::array<std::uint32_t, 160> offsets{};
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        offsets[index] = readU32(0x40 + index * sizeof(std::uint32_t));
    }
    const std::size_t countBase = offsets[0];
    if (countBase + 26 * sizeof(std::uint32_t) > data.size()) {
        std::cerr << "The moc3 count section is invalid.\n";
        return false;
    }
    const std::uint32_t artMeshCount = readU32(countBase + 4 * sizeof(std::uint32_t));
    const std::uint32_t colorCount = readU32(countBase + 23 * sizeof(std::uint32_t));
    const std::uint32_t secondColorCount = readU32(countBase + 24 * sizeof(std::uint32_t));
    if (colorCount != secondColorCount
        || offsets[42] + artMeshCount > data.size()) {
        std::cerr << "The moc3 effect sections have an unsupported format.\n";
        return false;
    }

    for (std::uint32_t index = 0; index < artMeshCount; ++index) {
        data[offsets[42] + index] &= 0xFC;
    }
    for (int section = 108; section <= 113; ++section) {
        const std::size_t sectionEnd = offsets[section]
            + static_cast<std::size_t>(colorCount) * sizeof(float);
        if (sectionEnd > data.size()) {
            std::cerr << "The moc3 color sections have an unsupported format.\n";
            return false;
        }
        const float neutralValue = section <= 110 ? 1.0f : 0.0f;
        for (std::uint32_t index = 0; index < colorCount; ++index) {
            writeFloat(offsets[section] + index * sizeof(float), neutralValue);
        }
    }

    std::ofstream output(moc3File, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output) {
        std::cerr << "Could not write the moc3 file.\n";
        return false;
    }
    std::cout << "Removed blend and color effects from " << moc3File.filename().string() << ".\n";
    return true;
}

bool writeColorMap(
    const fs::path& folder,
    const std::vector<fs::path>& paths,
    const std::vector<int>& counts,
    const std::vector<cv::Vec3b>& palette
) {
    cv::FileStorage storage(
        (folder / colorMapFileName).string(),
        cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON
    );
    if (!storage.isOpened()) {
        return false;
    }

    storage << "colors" << "[";
    int offset = 0;
    for (std::size_t fileIndex = 0; fileIndex < paths.size(); ++fileIndex) {
        for (int component = 0; component < counts[fileIndex]; ++component) {
            const cv::Vec3b color = palette[offset + component];
            storage << "{";
            storage << "red" << static_cast<int>(color[2]);
            storage << "green" << static_cast<int>(color[1]);
            storage << "blue" << static_cast<int>(color[0]);
            storage << "file" << paths[fileIndex].filename().string();
            storage << "}";
        }
        offset += counts[fileIndex];
    }
    storage << "]";
    return true;
}

int main() {
    fs::path modelRoot;
    if (!getModelRoot(modelRoot)) {
        std::cerr << "COLORFINDER_IMAGE_DIR is missing or is not a valid model folder. "
                     "Run setup.exe first.\n";
        return 1;
    }
    fs::path folder;
    if (!colorfinder::findTextureFolder(modelRoot, folder)) {
        std::cerr << "Could not find a texture folder ending in .<number> under the model folder.\n";
        return 1;
    }
    std::vector<fs::path> paths;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
        if (isImage(entry.path())) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    if (paths.empty()) {
        std::cout << "No supported images found.\n";
        return 0;
    }

    std::vector<cv::Mat> images;
    std::vector<fs::path> validPaths;
    std::vector<int> counts;
    int totalComponents = 0;
    for (const fs::path& path : paths) {
        cv::Mat image = loadRgba(path);
        if (image.empty()) {
            continue;
        }
        const int count = countComponents(image);
        images.push_back(image);
        validPaths.push_back(path);
        counts.push_back(count);
        totalComponents += count;
    }

    if (totalComponents > maxPaletteColors) {
        std::cerr << "Error: " << totalComponents << " textures were found, but the "
                  << "palette supports only " << maxPaletteColors << " unique colors.\n"
                  << "Continuing could create duplicate texture colors.\n"
                  << "Press Enter to close.\n";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
        return 1;
    }

    const std::vector<cv::Vec3b> palette = buildPalette(totalComponents);
    fs::path moc3File;
    if (!colorfinder::findMoc3File(modelRoot, moc3File)) {
        std::cerr << "Could not find a .moc3 file in the model folder.\n";
        return 1;
    }
    if (!removeMoc3Effects(moc3File)) {
        return 1;
    }
    if (!writeColorMap(folder, validPaths, counts, palette)) {
        std::cerr << "Could not write " << colorMapFileName << ".\n";
        return 1;
    }

    int offset = 0;
    for (std::size_t index = 0; index < images.size(); ++index) {
        cv::Mat output = colorImage(images[index], offset, palette);
        cv::imwrite(validPaths[index].string(), output);
        std::cout << validPaths[index].filename().string() << ": " << counts[index]
                  << " texture(s) recolored in place\n";
        offset += counts[index];
    }
    std::cout << "Colored " << totalComponents << " texture(s) across "
              << images.size() << " image(s).\n";
}