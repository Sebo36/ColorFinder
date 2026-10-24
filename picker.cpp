#define NOMINMAX
#include <windows.h>
#include "model_paths.h"

#include <opencv2/core/persistence.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::atomic<bool> running{true};
std::atomic<bool> pickingEnabled{true};
bool exitWhenWindowsClose = false;
std::mutex colorMutex;
std::queue<cv::Vec3b> pickedColors;
std::set<std::string> matchWindows;
constexpr int colorRadius = 8;
constexpr char colorMapFileName[] = "colorfinder_colors.json";
HWND statusWindow = nullptr;
HWND instructionLabel = nullptr;
HWND pickedColorLabel = nullptr;
HWND statusLabel = nullptr;
HBRUSH statusBackgroundBrush = nullptr;
HFONT statusRegularFont = nullptr;
HFONT statusSemiboldFont = nullptr;

constexpr COLORREF statusBackgroundColor = RGB(18, 22, 30);
constexpr COLORREF statusTextColor = RGB(235, 240, 245);
constexpr COLORREF statusAccentColor = RGB(70, 190, 170);

const std::set<std::string> imageExtensions = {
    ".bmp", ".gif", ".jpeg", ".jpg", ".png", ".tif", ".tiff", ".webp"
};

struct ColorMapEntry {
    cv::Vec3b color;
    fs::path file;
};

std::vector<ColorMapEntry> colorMap;

bool getImageFolder(fs::path& folder) {
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
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return fs::is_regular_file(path) && imageExtensions.count(extension) != 0;
}

bool loadColorMap(const fs::path& folder) {
    cv::FileStorage storage(
        (folder / colorMapFileName).string(),
        cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON
    );
    if (!storage.isOpened()) {
        return false;
    }

    const cv::FileNode colors = storage["colors"];
    if (colors.type() != cv::FileNode::SEQ) {
        return false;
    }

    for (const cv::FileNode& entry : colors) {
        int red = 0;
        int green = 0;
        int blue = 0;
        std::string file;
        entry["red"] >> red;
        entry["green"] >> green;
        entry["blue"] >> blue;
        entry["file"] >> file;
        if (file.empty() || red < 0 || red > 255 || green < 0 || green > 255
            || blue < 0 || blue > 255) {
            continue;
        }
        colorMap.push_back({
            cv::Vec3b(
                static_cast<unsigned char>(blue),
                static_cast<unsigned char>(green),
                static_cast<unsigned char>(red)
            ),
            folder / file
        });
    }
    return !colorMap.empty();
}

const ColorMapEntry* findMappedColor(const cv::Vec3b& color) {
    const int radiusSquared = colorRadius * colorRadius;
    const ColorMapEntry* nearest = nullptr;
    int nearestDistance = radiusSquared + 1;
    for (const ColorMapEntry& entry : colorMap) {
        const int blueDifference = color[0] - entry.color[0];
        const int greenDifference = color[1] - entry.color[1];
        const int redDifference = color[2] - entry.color[2];
        const int distance = blueDifference * blueDifference
            + greenDifference * greenDifference
            + redDifference * redDifference;
        if (distance <= radiusSquared && distance < nearestDistance) {
            nearest = &entry;
            nearestDistance = distance;
        }
    }
    return nearest;
}

void restoreSystemCursor() {
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
}

bool setCrosshairCursor() {
    HCURSOR source = LoadCursorW(
        nullptr, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(32515))
    );
    HCURSOR copy = static_cast<HCURSOR>(CopyImage(source, IMAGE_CURSOR, 0, 0, LR_COPYFROMRESOURCE));
    return copy != nullptr && SetSystemCursor(copy, 32512);
}

void queueScreenColor(POINT point) {
    HDC desktop = GetDC(nullptr);
    COLORREF color = desktop == nullptr ? CLR_INVALID : GetPixel(desktop, point.x, point.y);
    if (desktop != nullptr) {
        ReleaseDC(nullptr, desktop);
    }
    if (color == CLR_INVALID) {
        return;
    }

    cv::Vec3b bgr(
        GetBValue(color),
        GetGValue(color),
        GetRValue(color)
    );
    std::lock_guard<std::mutex> lock(colorMutex);
    pickedColors.push(bgr);
    std::cout << "COLOR: rgb(" << static_cast<int>(bgr[2]) << ", "
              << static_cast<int>(bgr[1]) << ", " << static_cast<int>(bgr[0]) << ")\n";
}

LRESULT CALLBACK mouseHook(int code, WPARAM message, LPARAM data) {
    if (code >= 0 && message == WM_LBUTTONDOWN && pickingEnabled) {
        const MSLLHOOKSTRUCT* mouseData = reinterpret_cast<MSLLHOOKSTRUCT*>(data);
        queueScreenColor(mouseData->pt);
    }
    return CallNextHookEx(nullptr, code, message, data);
}

LRESULT CALLBACK keyboardHook(int code, WPARAM message, LPARAM data) {
    if (code >= 0 && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
        const KBDLLHOOKSTRUCT* keyboardData = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        if (keyboardData->vkCode == VK_ESCAPE) {
            running = false;
            PostQuitMessage(0);
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        running = false;
        PostQuitMessage(0);
        return TRUE;
    }
    return FALSE;
}

void setStatus(const wchar_t* text) {
    if (statusLabel != nullptr) {
        SetWindowTextW(statusLabel, text);
        InvalidateRect(statusLabel, nullptr, TRUE);
        UpdateWindow(statusLabel);
    }
}

void setPickedColor(const cv::Vec3b& bgr) {
    if (pickedColorLabel == nullptr) {
        return;
    }
    wchar_t text[80]{};
    swprintf_s(
        text,
        L"Picked color: RGB(%d, %d, %d)",
        static_cast<int>(bgr[2]),
        static_cast<int>(bgr[1]),
        static_cast<int>(bgr[0])
    );
    SetWindowTextW(pickedColorLabel, text);
    InvalidateRect(pickedColorLabel, nullptr, TRUE);
    UpdateWindow(pickedColorLabel);
}

LRESULT CALLBACK statusWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CTLCOLORSTATIC) {
        HDC deviceContext = reinterpret_cast<HDC>(wParam);
        SetTextColor(
            deviceContext,
            reinterpret_cast<HWND>(lParam) == statusLabel
                ? statusAccentColor
                : statusTextColor
        );
        SetBkMode(deviceContext, TRANSPARENT);
        return reinterpret_cast<LRESULT>(statusBackgroundBrush);
    }
    if (message == WM_ERASEBKGND) {
        RECT clientArea{};
        GetClientRect(window, &clientArea);
        FillRect(reinterpret_cast<HDC>(wParam), &clientArea, statusBackgroundBrush);
        return 1;
    }
    if (message == WM_CLOSE) {
        running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool createStatusWindow(HINSTANCE instance) {
    const wchar_t className[] = L"ColorFinderPickerStatus";
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = statusWindowProcedure;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(
        nullptr, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(32512))
    );
    statusBackgroundBrush = CreateSolidBrush(statusBackgroundColor);
    statusRegularFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    statusSemiboldFont = CreateFontW(
        -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    windowClass.hbrBackground = statusBackgroundBrush;
    RegisterClassW(&windowClass);

    statusWindow = CreateWindowExW(
        WS_EX_TOPMOST,
        className,
        L"ColorFinder picker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        400,
        200,
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (statusWindow == nullptr) {
        return false;
    }

    instructionLabel = CreateWindowW(
        L"STATIC",
        L"Pick a color anywhere on screen",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        24,
        18,
        352,
        24,
        statusWindow,
        nullptr,
        instance,
        nullptr
    );
    SendMessageW(instructionLabel, WM_SETFONT, reinterpret_cast<WPARAM>(statusSemiboldFont), TRUE);
    pickedColorLabel = CreateWindowW(
        L"STATIC",
        L"Picked color: none",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        24,
        52,
        352,
        24,
        statusWindow,
        nullptr,
        instance,
        nullptr
    );
    SendMessageW(pickedColorLabel, WM_SETFONT, reinterpret_cast<WPARAM>(statusRegularFont), TRUE);
    statusLabel = CreateWindowW(
        L"STATIC",
        L"Waiting for a color...",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        24,
        86,
        352,
        28,
        statusWindow,
        nullptr,
        instance,
        nullptr
    );
    SendMessageW(statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(statusSemiboldFont), TRUE);
    ShowWindow(statusWindow, SW_SHOW);
    UpdateWindow(statusWindow);
    return instructionLabel != nullptr && pickedColorLabel != nullptr
        && statusLabel != nullptr;
}

cv::Mat textureDisplay(const cv::Mat& bgr, const cv::Mat& alpha) {
    cv::Mat display(bgr.size(), CV_8UC3);
    for (int row = 0; row < bgr.rows; ++row) {
        for (int column = 0; column < bgr.cols; ++column) {
            const int shade = ((row / 32 + column / 32) % 2 == 0) ? 220 : 255;
            const float opacity = alpha.at<unsigned char>(row, column) / 255.0f;
            const cv::Vec3b grey(128, 128, 128);
            for (int channel = 0; channel < 3; ++channel) {
                display.at<cv::Vec3b>(row, column)[channel] = static_cast<unsigned char>(
                    grey[channel] * opacity + shade * (1.0f - opacity)
                );
            }
        }
    }
    return display;
}

bool searchImage(const cv::Vec3b& color, const fs::path& imagePath) {
        cv::Mat loaded = cv::imread(imagePath.string(), cv::IMREAD_UNCHANGED);
        if (loaded.empty()) {
            return false;
        }

        cv::Mat bgr;
        cv::Mat alpha;
        if (loaded.channels() == 4) {
            cv::extractChannel(loaded, alpha, 3);
            cv::cvtColor(loaded, bgr, cv::COLOR_BGRA2BGR);
        } else if (loaded.channels() == 3) {
            bgr = loaded;
            alpha = cv::Mat(loaded.size(), CV_8UC1, cv::Scalar(255));
        } else {
            cv::cvtColor(loaded, bgr, cv::COLOR_GRAY2BGR);
            alpha = cv::Mat(loaded.size(), CV_8UC1, cv::Scalar(255));
        }

        cv::Mat matching(bgr.size(), CV_8UC1, cv::Scalar(0));
        const int radiusSquared = colorRadius * colorRadius;
        for (int row = 0; row < bgr.rows; ++row) {
            for (int column = 0; column < bgr.cols; ++column) {
                if (alpha.at<unsigned char>(row, column) == 0) {
                    continue;
                }
                const cv::Vec3b pixel = bgr.at<cv::Vec3b>(row, column);
                const int blueDifference = pixel[0] - color[0];
                const int greenDifference = pixel[1] - color[1];
                const int redDifference = pixel[2] - color[2];
                const int distanceSquared = blueDifference * blueDifference
                    + greenDifference * greenDifference
                    + redDifference * redDifference;
                if (distanceSquared <= radiusSquared) {
                    matching.at<unsigned char>(row, column) = 255;
                }
            }
        }
        const int count = cv::countNonZero(matching);
        if (count == 0) {
            return false;
        }

        cv::Mat display = textureDisplay(bgr, alpha);
        display.setTo(cv::Scalar(0, 0, 255), matching);
        const std::string window = "Color match - " + imagePath.filename().string();
        matchWindows.insert(window);
        cv::namedWindow(window, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
        cv::setWindowProperty(window, cv::WND_PROP_ASPECT_RATIO, cv::WINDOW_KEEPRATIO);
        cv::resizeWindow(window, std::max(1, static_cast<int>(bgr.cols * 0.25)),
                         std::max(1, static_cast<int>(bgr.rows * 0.25)));
        cv::imshow(window, display);
        std::cout << "FOUND " << count << " matching pixel(s): " << imagePath.filename().string() << '\n';
        return true;
}

void processColors() {
    cv::Vec3b color;
    {
        std::lock_guard<std::mutex> lock(colorMutex);
        if (pickedColors.empty()) {
            return;
        }
        color = pickedColors.front();
        pickedColors.pop();
    }
    setPickedColor(color);
    const ColorMapEntry* mappedColor = findMappedColor(color);
    const bool found = mappedColor != nullptr
        && fs::is_regular_file(mappedColor->file)
        && searchImage(color, mappedColor->file);
    setStatus(found ? L"Texture found" : L"No matching Texture found");
    if (found) {
        pickingEnabled = false;
        exitWhenWindowsClose = true;
    }
}

void updateWindows() {
    for (auto iterator = matchWindows.begin(); iterator != matchWindows.end();) {
        if (cv::getWindowProperty(*iterator, cv::WND_PROP_VISIBLE) < 1) {
            iterator = matchWindows.erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (matchWindows.empty()) {
        if (exitWhenWindowsClose) {
            running = false;
            PostQuitMessage(0);
        } else {
            pickingEnabled = true;
        }
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!setCrosshairCursor()) {
        std::cerr << "Could not set the system cursor.\n";
    } else {
        std::atexit(restoreSystemCursor);
    }
    if (!createStatusWindow(instance)) {
        std::cerr << "Could not create the picker status window.\n";
        running = false;
    }
    if (running) {
        fs::path folder;
        if (!getImageFolder(folder)) {
            std::cerr << "COLORFINDER_IMAGE_DIR is missing or is not a valid folder. "
                         "Run setup.exe first.\n";
            running = false;
        } else {
            fs::path textureFolder;
            if (!colorfinder::findTextureFolder(folder, textureFolder)
                || !loadColorMap(textureFolder)) {
                std::cerr << "Could not find the numbered texture folder or read "
                          << colorMapFileName << ". Run imageColorer.exe first.\n";
                running = false;
            }
        }
    }
    SetConsoleCtrlHandler(consoleHandler, TRUE);
    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(nullptr), 0);
    HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW(nullptr), 0);
    if (mouse == nullptr || keyboard == nullptr) {
        std::cerr << "Could not install global input hooks.\n";
        running = false;
    }

    MSG message{};
    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (pickingEnabled) {
            processColors();
        } else {
            updateWindows();
        }
        cv::waitKey(10);
    }

    if (mouse != nullptr) {
        UnhookWindowsHookEx(mouse);
    }
    if (keyboard != nullptr) {
        UnhookWindowsHookEx(keyboard);
    }
    cv::destroyAllWindows();
    if (statusWindow != nullptr) {
        DestroyWindow(statusWindow);
    }
    DeleteObject(statusBackgroundBrush);
    DeleteObject(statusRegularFont);
    DeleteObject(statusSemiboldFont);
    return 0;
}