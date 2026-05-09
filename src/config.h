#pragma once
#include <string>

struct Config {
    std::string defaultMode;
    std::string outDir;
    std::string startupDir;
    bool force = false;
    bool debug = false;
    bool noBackup = false;
    std::string fileReadMode = "text";
    bool confirmExec = false;
    size_t maxClipboardSize = 5 * 1024 * 1024;
    size_t maxReadSize = 10 * 1024 * 1024;
    bool easyMode = false;  // 新增

    bool load(const std::string& path);
    void save(const std::string& path) const;
};