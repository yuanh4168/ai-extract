#include "pathutil.h"
#include "utils.h"
#include <windows.h>
#include <direct.h>
#include <unordered_set>
#include <functional>

bool directoryExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

bool fileExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

void makeDirectory(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == '\\' || path[i] == '/' || i == path.size() - 1)
            if (!current.empty() && !directoryExists(current)) _mkdir(current.c_str());
    }
}

std::string fullPath(const std::string& relative) {
    char full[MAX_PATH];
    if (_fullpath(full, relative.c_str(), MAX_PATH)) return std::string(full);
    return relative;
}

static bool isReservedDeviceName(const std::string& name) {
    static const std::unordered_set<std::string> reserved = {
        "con","prn","aux","nul",
        "com1","com2","com3","com4","com5","com6","com7","com8","com9",
        "lpt1","lpt2","lpt3","lpt4","lpt5","lpt6","lpt7","lpt8","lpt9"
    };
    std::string base = name;
    size_t dot = base.find('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return reserved.count(toLower(base)) > 0;
}

bool isPathSafe(const std::string& baseDirAbs, const std::string& relPath, std::string& outAbsolutePath) {
    std::string native = relPath;
    std::replace(native.begin(), native.end(), '/', '\\');
    std::string tentative = baseDirAbs + "\\" + native;
    char absolute[MAX_PATH];
    if (!GetFullPathNameA(tentative.c_str(), MAX_PATH, absolute, nullptr)) return false;
    outAbsolutePath = std::string(absolute);
    std::string lowerAbs = toLower(outAbsolutePath);
    std::string lowerBase = toLower(baseDirAbs);
    if (lowerAbs.compare(0, lowerBase.size(), lowerBase) != 0) return false;
    if (lowerAbs.size() > lowerBase.size() && lowerAbs[lowerBase.size()] != '\\') return false;
    std::string remaining = outAbsolutePath.substr(lowerBase.size());
    if (!remaining.empty() && remaining[0] == '\\') remaining.erase(0, 1);
    std::string component;
    for (char ch : remaining) {
        if (ch == '\\') {
            if (!component.empty() && isReservedDeviceName(component)) return false;
            component.clear();
        } else component.push_back(ch);
    }
    if (!component.empty() && isReservedDeviceName(component)) return false;
    return true;
}

std::string getDirectoryTree(const std::string& root) {
    std::string result;
    std::function<void(const std::string&, const std::string&)> recurse = [&](const std::string& dir, const std::string& prefix) {
        std::string searchPath = dir + "\\*";
        WIN32_FIND_DATAA ffd;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &ffd);
        if (hFind == INVALID_HANDLE_VALUE) return;
        std::vector<std::string> entries;
        do {
            if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
            entries.push_back(ffd.cFileName);
        } while (FindNextFileA(hFind, &ffd) != 0);
        FindClose(hFind);
        std::sort(entries.begin(), entries.end());
        for (size_t i = 0; i < entries.size(); ++i) {
            bool last = (i == entries.size() - 1);
            std::string entry = dir + "\\" + entries[i];
            if (directoryExists(entry)) {
                result += prefix + (last ? "└── " : "├── ") + entries[i] + "/\n";
                recurse(entry, prefix + (last ? "    " : "│   "));
            } else {
                result += prefix + (last ? "└── " : "├── ") + entries[i] + "\n";
            }
        }
    };
    if (directoryExists(root)) {
        result += root + "\n";
        recurse(root, "");
    }
    return result;
}