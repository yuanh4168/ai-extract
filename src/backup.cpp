#include "backup.h"
#include "pathutil.h"
#include "utils.h"
#include <chrono>
#include <ctime>
#include <sstream>
#include <windows.h>

static std::string makeTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

bool gitBackup(const std::string& targetDir) {
    std::string oldDir = getCurrentDir();
    if (!SetCurrentDirectoryA(targetDir.c_str())) {
        CLR_ERROR << "[git] 无法进入目录 " << targetDir << "\n";
        return false;
    }
    if (!directoryExists(".git")) {
        if (std::system("git init") != 0) {
            CLR_ERROR << "[git] git init failed\n";
            SetCurrentDirectoryA(oldDir.c_str());
            return false;
        }
    }
    if (std::system("git add -A") != 0) {
        CLR_ERROR << "[git] git add failed\n";
        SetCurrentDirectoryA(oldDir.c_str());
        return false;
    }
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    std::string msg = "auto backup at " + oss.str();
    std::string cmd = "git -c user.name=ai-extract -c user.email=ai-extract@local commit -m \"" + msg + "\"";
    int rc = std::system(cmd.c_str());
    SetCurrentDirectoryA(oldDir.c_str());
    if (rc != 0) { CLR_ERROR << "[git] git commit failed\n"; return false; }
    CLR_SUCCESS << "[git] committed.\n";
    return true;
}

bool zipBackup(const std::string& targetDir) {
    std::string parent = targetDir;
    while (!parent.empty() && (parent.back() == '/' || parent.back() == '\\')) parent.pop_back();
    size_t pos = parent.find_last_of("/\\");
    if (pos != std::string::npos) parent = parent.substr(0, pos);
    else parent = ".";
    std::string zipName = "project_backup_" + makeTimestamp() + ".zip";
    std::string zipPath = parent + "\\" + zipName;
    std::string cmd = "powershell Compress-Archive -Path \"" + targetDir + "\" -DestinationPath \"" + zipPath + "\" -Force";
    int rc = std::system(cmd.c_str());
    if (rc != 0) { CLR_ERROR << "[zip] 压缩失败\n"; return false; }
    CLR_SUCCESS << "[zip] Archive: " << zipPath << "\n";
    return true;
}