#include <windows.h>
#include <shobjidl.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

constexpr wchar_t environmentVariable[] = L"COLORFINDER_IMAGE_DIR";
constexpr int pathEditId = 1001;
constexpr int browseButtonId = 1002;
constexpr int saveButtonId = 1003;
HWND pathEdit = nullptr;
HBRUSH backgroundBrush = nullptr;
HBRUSH editBrush = nullptr;
HFONT regularFont = nullptr;
HFONT semiboldFont = nullptr;

constexpr COLORREF backgroundColor = RGB(18, 22, 30);
constexpr COLORREF editColor = RGB(31, 39, 52);
constexpr COLORREF textColor = RGB(235, 240, 245);
constexpr COLORREF accentColor = RGB(70, 190, 170);

void showError(const wchar_t* message) {
    MessageBoxW(nullptr, message, L"ColorFinder setup", MB_OK | MB_ICONERROR);
}

bool saveUserEnvironmentVariable(const std::wstring& folder) {
    if (!SetEnvironmentVariableW(environmentVariable, folder.c_str())) {
        return false;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &key
        ) != ERROR_SUCCESS) {
        return false;
    }

    const LONG result = RegSetValueExW(
        key,
        environmentVariable,
        0,
        REG_EXPAND_SZ,
        reinterpret_cast<const BYTE*>(folder.c_str()),
        static_cast<DWORD>((folder.size() + 1) * sizeof(wchar_t))
    );
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    SendMessageTimeoutW(
        HWND_BROADCAST,
        WM_SETTINGCHANGE,
        0,
        reinterpret_cast<LPARAM>(L"Environment"),
        SMTO_ABORTIFHUNG,
        5000,
        nullptr
    );
    return true;
}

void browseForFolder(HWND owner) {
    IFileDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)
    );
    if (FAILED(result)) {
        showError(L"Could not open the folder browser.");
        return;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Choose the model root folder");

    result = dialog->Show(owner);
    if (SUCCEEDED(result)) {
        IShellItem* item = nullptr;
        result = dialog->GetResult(&item);
        if (SUCCEEDED(result)) {
            PWSTR path = nullptr;
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(result)) {
                SetWindowTextW(pathEdit, path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}

std::wstring getPathFromEdit() {
    const int length = GetWindowTextLengthW(pathEdit);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
    GetWindowTextW(pathEdit, buffer.data(), length + 1);
    return buffer.data();
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CTLCOLORSTATIC: {
        HDC deviceContext = reinterpret_cast<HDC>(wParam);
        SetTextColor(deviceContext, textColor);
        SetBkMode(deviceContext, TRANSPARENT);
        return reinterpret_cast<LRESULT>(backgroundBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC deviceContext = reinterpret_cast<HDC>(wParam);
        SetTextColor(deviceContext, textColor);
        SetBkColor(deviceContext, editColor);
        return reinterpret_cast<LRESULT>(editBrush);
    }
    case WM_ERASEBKGND: {
        RECT clientArea{};
        GetClientRect(window, &clientArea);
        FillRect(reinterpret_cast<HDC>(wParam), &clientArea, backgroundBrush);
        return 1;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (drawItem->CtlType != ODT_BUTTON) {
            break;
        }
        const bool saveButton = drawItem->CtlID == saveButtonId;
        const bool pressed = (drawItem->itemState & ODS_SELECTED) != 0;
        const COLORREF fillColor = saveButton ? accentColor : editColor;
        HBRUSH buttonBrush = CreateSolidBrush(pressed ? RGB(55, 150, 138) : fillColor);
        HPEN buttonPen = CreatePen(PS_SOLID, 1, saveButton ? accentColor : RGB(70, 85, 105));
        HGDIOBJ oldBrush = SelectObject(drawItem->hDC, buttonBrush);
        HGDIOBJ oldPen = SelectObject(drawItem->hDC, buttonPen);
        RoundRect(
            drawItem->hDC,
            drawItem->rcItem.left,
            drawItem->rcItem.top,
            drawItem->rcItem.right,
            drawItem->rcItem.bottom,
            8,
            8
        );
        SelectObject(drawItem->hDC, oldBrush);
        SelectObject(drawItem->hDC, oldPen);
        DeleteObject(buttonBrush);
        DeleteObject(buttonPen);
        SetBkMode(drawItem->hDC, TRANSPARENT);
        SetTextColor(drawItem->hDC, textColor);
        DrawTextW(
            drawItem->hDC,
            saveButton ? L"Save" : L"Browse...",
            -1,
            const_cast<RECT*>(&drawItem->rcItem),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == browseButtonId) {
            browseForFolder(window);
            return 0;
        }
        if (LOWORD(wParam) == saveButtonId) {
            const std::wstring folder = getPathFromEdit();
            if (folder.empty() || !fs::is_directory(fs::path(folder))) {
                showError(L"Enter or choose a valid folder.");
                return 0;
            }
            if (!saveUserEnvironmentVariable(folder)) {
                showError(L"Could not save COLORFINDER_IMAGE_DIR.");
                return 0;
            }
            MessageBoxW(
                window,
                L"Folder saved. You can now launch picker.exe or imageColorer.exe.",
                L"ColorFinder setup",
                MB_OK | MB_ICONINFORMATION
            );
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const wchar_t className[] = L"ColorFinderSetupWindow";
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(
        nullptr, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(32512))
    );
    backgroundBrush = CreateSolidBrush(backgroundColor);
    editBrush = CreateSolidBrush(editColor);
    regularFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    semiboldFont = CreateFontW(
        -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    windowClass.hbrBackground = backgroundBrush;
    RegisterClassW(&windowClass);

    HWND window = CreateWindowExW(
        0,
        className,
        L"ColorFinder setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        660,
        190,
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (window == nullptr) {
        CoUninitialize();
        return 1;
    }

    HWND folderLabel = CreateWindowW(
        L"STATIC",
        L"Model root folder:",
        WS_VISIBLE | WS_CHILD,
        24,
        20,
        250,
        24,
        window,
        nullptr,
        instance,
        nullptr
    );
    SendMessageW(folderLabel, WM_SETFONT, reinterpret_cast<WPARAM>(semiboldFont), TRUE);
    pathEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        24,
        52,
        470,
        32,
        window,
        reinterpret_cast<HMENU>(pathEditId),
        instance,
        nullptr
    );
    SendMessageW(pathEdit, WM_SETFONT, reinterpret_cast<WPARAM>(regularFont), TRUE);
    wchar_t existingFolder[32768]{};
    const DWORD existingLength = GetEnvironmentVariableW(
        environmentVariable,
        existingFolder,
        static_cast<DWORD>(std::size(existingFolder))
    );
    if (existingLength > 0 && existingLength < std::size(existingFolder)) {
        SetWindowTextW(pathEdit, existingFolder);
    }
    HWND browseButton = CreateWindowW(
        L"BUTTON",
        L"Browse...",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        510,
        52,
        120,
        32,
        window,
        reinterpret_cast<HMENU>(browseButtonId),
        instance,
        nullptr
    );
    HWND saveButton = CreateWindowW(
        L"BUTTON",
        L"Save",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        510,
        96,
        120,
        32,
        window,
        reinterpret_cast<HMENU>(saveButtonId),
        instance,
        nullptr
    );
    SendMessageW(browseButton, WM_SETFONT, reinterpret_cast<WPARAM>(semiboldFont), TRUE);
    SendMessageW(saveButton, WM_SETFONT, reinterpret_cast<WPARAM>(semiboldFont), TRUE);

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    DeleteObject(backgroundBrush);
    DeleteObject(editBrush);
    DeleteObject(regularFont);
    DeleteObject(semiboldFont);
    return static_cast<int>(message.wParam);
}