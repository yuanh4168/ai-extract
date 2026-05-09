#include "clipboard.h"
#include "utils.h"
#include <windows.h>

std::string readClipboard() {
    if (!OpenClipboard(nullptr)) throw std::runtime_error("Cannot open clipboard");

    // 修改点：检查是否有文本格式，若无则返回空字符串
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        CloseClipboard();
        return "";
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return "";  // 获取失败返回空
    }

    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (!pszText) {
        CloseClipboard();
        return "";
    }

    std::wstring wstr(pszText);
    GlobalUnlock(hData);
    CloseClipboard();

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &utf8[0], sizeNeeded, nullptr, nullptr);
    return utf8;
}

void writeClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) { CLR_ERROR << "无法打开剪贴板进行写入\n"; return; }
    EmptyClipboard();

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) { CloseClipboard(); CLR_ERROR << "编码转换失败\n"; return; }

    std::wstring wstr(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wstr[0], wideLen);

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wstr.size() + 1) * sizeof(wchar_t));
    if (hMem) {
        wchar_t* pData = static_cast<wchar_t*>(GlobalLock(hMem));
        if (pData) {
            wcscpy(pData, wstr.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
    CLR_SUCCESS << "已将内容复制到剪贴板\n";
}