#include "directiveproc.h"
#include "config.h"
#include "clipboard.h"
#include "pathutil.h"
#include "parser.h"
#include "backup.h"
#include "utils.h"
#include "web_browser.h"
#include <fstream>
#include <sstream>

static bool askUser(const std::string& question) {
    CLR_INPUT << question << " [y/N] ";
    std::string ans;
    std::getline(std::cin, ans);
    return ans == "y" || ans == "Y";
}

static bool execCommand(const std::string& userCmd, std::string& output, int& exitCode) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) { output = "创建管道失败"; return false; }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;
    std::string cmdline = "cmd.exe /c " + userCmd + " 2>&1";
    if (!CreateProcessA(NULL, const_cast<char*>(cmdline.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWrite);
        CloseHandle(hRead);
        output = "创建进程失败";
        return false;
    }
    CloseHandle(hWrite);
    char buf[256];
    DWORD bytesRead;
    output.clear();
    while (ReadFile(hRead, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0)
        output.append(buf, bytesRead);
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD dwExit;
    GetExitCodeProcess(pi.hProcess, &dwExit);
    exitCode = static_cast<int>(dwExit);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

static void handleReadDirectives(const std::vector<FileDirective>& directives, const std::string& baseDirAbs, const Config& cfg) {
    for (const auto& d : directives) {
        if (d.type != FileDirective::READ_FILE) continue;
        std::string safeAbsPath;
        if (!isPathSafe(baseDirAbs, d.path, safeAbsPath)) {
            CLR_WARN << "[READ] 路径不安全，已跳过: " << d.path << "\n";
            writeLog(LogLevel::WARN, "[READ] Unsafe path skipped: " + d.path);
            continue;
        }
        if (!fileExists(safeAbsPath)) {
            CLR_WARN << "[READ] 文件不存在: " << safeAbsPath << "\n";
            writeLog(LogLevel::WARN, "[READ] File not found: " + safeAbsPath);
            continue;
        }
        std::ifstream fileSizeCheck(safeAbsPath, std::ios::ate | std::ios::binary);
        size_t fileSize = fileSizeCheck.tellg();
        fileSizeCheck.close();
        if (cfg.fileReadMode == "text" && fileSize > cfg.maxReadSize) {
            CLR_WARN << "[READ] 文件过大 (" << fileSize << " 字节)，超出限制 (" << cfg.maxReadSize << ")，自动切换为仅复制路径。\n";
            writeLog(LogLevel::WARN, "[READ] Large file fallback to path: " + safeAbsPath);
            writeClipboard(safeAbsPath);
            continue;
        }
        if (cfg.fileReadMode == "path") {
            writeClipboard(safeAbsPath);
            CLR_INFO << "文件路径已复制到剪贴板: " << safeAbsPath << "\n";
        } else {
            std::ifstream in(safeAbsPath, std::ios::binary);
            if (!in) { CLR_ERROR << "[READ] 无法打开文件: " << safeAbsPath << "\n"; continue; }
            std::ostringstream oss;
            oss << in.rdbuf();
            std::string content = oss.str();
            writeClipboard(content);
            CLR_INFO << "文件内容 (" << content.size() << " 字节) 已复制到剪贴板\n";
        }
        writeLog(LogLevel::INFO, "[READ] Processed file: " + safeAbsPath);
    }
}

static void handleDeleteDirectives(const std::vector<FileDirective>& directives, const std::string& baseDirAbs, const Config&) {
    std::vector<std::string> safePaths;
    for (const auto& d : directives) {
        if (d.type != FileDirective::DELETE_FILE) continue;
        std::string absPath;
        if (!isPathSafe(baseDirAbs, d.path, absPath)) {
            CLR_WARN << "[DELETE] 路径不安全，已跳过: " << d.path << "\n";
            continue;
        }
        safePaths.push_back(absPath);
    }
    if (safePaths.empty()) return;
    CLR_WARN << "\n检测到文件删除指令。即将删除以下文件:\n";
    for (const auto& p : safePaths) CLR_WARN << "  - " << p << "\n";
    if (!askUser("确认删除？(第一次确认)")) { CLR_INFO << "取消删除\n"; return; }
    if (!askUser("请再次确认：永久删除？(第二次确认)")) { CLR_INFO << "取消删除\n"; return; }
    for (const auto& p : safePaths) {
        if (DeleteFileA(p.c_str())) {
            CLR_SUCCESS << "[DELETE] 已删除: " << p << "\n";
            writeLog(LogLevel::SUCCESS, "[DELETE] Deleted " + p);
        } else {
            CLR_ERROR << "[DELETE] 删除失败: " << p << "\n";
            writeLog(LogLevel::ERROR, "[DELETE] Failed to delete " + p);
        }
    }
}

static void handleExecDirectives(const std::vector<FileDirective>& directives, const Config& cfg) {
    for (const auto& d : directives) {
        if (d.type != FileDirective::EXEC_COMMAND) continue;
        std::string cmd = trim(d.content);
        if (cmd.empty()) continue;
        if (cfg.confirmExec) {
            CLR_INPUT << "即将执行命令: " << cmd << "\n是否继续? [y/N] ";
            std::string ans;
            std::getline(std::cin, ans);
            if (ans != "y" && ans != "Y") { CLR_INFO << "已跳过命令执行\n"; continue; }
        }
        std::string output;
        int exitCode = -1;
        if (execCommand(cmd, output, exitCode)) {
            if (exitCode == 0) {
                CLR_SUCCESS << "[EXEC] 成功\n";
                if (!output.empty()) CLR_INFO << output;
            } else {
                CLR_ERROR << "[EXEC] 失败 (退出码 " << exitCode << ")\n";
                if (!output.empty()) CLR_ERROR << output;
            }
            writeClipboard(output);
            writeLog(exitCode == 0 ? LogLevel::SUCCESS : LogLevel::ERROR, "[EXEC] " + cmd);
        } else {
            CLR_ERROR << "[EXEC] 无法执行命令\n";
            writeLog(LogLevel::ERROR, "[EXEC] Failed to execute: " + cmd);
        }
    }
}

// ★ 修改点：对空 URL 增加明确警告
static void handleBrowseDirectives(const std::vector<FileDirective>& directives, const Config& cfg) {
    std::string combined;
    bool first = true;
    for (const auto& d : directives) {
        if (d.type != FileDirective::BROWSE_PAGE) continue;
        std::string url = trim(d.path);
        if (url.empty()) {
            CLR_WARN << "[BROWSE] 指令缺少 URL，已跳过。\n";
            writeLog(LogLevel::WARN, "[BROWSE] Skipped directive with empty URL");
            continue;
        }
        CLR_INFO << "[BROWSE] 正在浏览: " << url << "\n";
        std::string result = browsePage(url, cfg.maxReadSize);
        if (result.find("Error:") == 0) {
            CLR_ERROR << "[BROWSE] " << result << "\n";
            writeLog(LogLevel::ERROR, "[BROWSE] " + result);
        } else {
            CLR_SUCCESS << "[BROWSE] 获取成功 (" << result.size() << " 字符)\n";
            writeLog(LogLevel::INFO, "[BROWSE] Fetched " + url + " (" + std::to_string(result.size()) + " chars)");
            if (!first) combined += "\n\n---\n\n";
            combined += "## 页面: " + url + "\n\n" + result;
            first = false;
        }
    }
    if (!combined.empty()) {
        writeClipboard(combined);
        CLR_SUCCESS << "[BROWSE] 内容已复制到剪贴板\n";
    }
}

void processDirectives(const std::vector<FileDirective>& directives, const Config& cfg) {
    std::string baseDirAbs = fullPath(cfg.outDir);
    int cCreate = 0, cRead = 0, cDelete = 0, cExec = 0, cBrowse = 0;
    for (auto& d : directives) {
        if (d.type == FileDirective::CREATE_FILE) cCreate++;
        else if (d.type == FileDirective::READ_FILE) cRead++;
        else if (d.type == FileDirective::DELETE_FILE) cDelete++;
        else if (d.type == FileDirective::EXEC_COMMAND) cExec++;
        else if (d.type == FileDirective::BROWSE_PAGE) cBrowse++;
    }
    if (cCreate + cRead + cDelete + cExec + cBrowse == 0) return;
    CLR_INFO << "指令统计: 创建 " << cCreate << " 个, 读取 " << cRead << " 个, 删除 " << cDelete 
             << " 个, 执行 " << cExec << " 个, 浏览 " << cBrowse << " 个\n";

    if (cDelete > 0) handleDeleteDirectives(directives, baseDirAbs, cfg);
    if (cBrowse > 0) handleBrowseDirectives(directives, cfg);

    std::vector<std::pair<std::string, std::string>> createFiles;
    for (auto& d : directives) {
        if (d.type != FileDirective::CREATE_FILE) continue;
        std::string absPath;
        if (!isPathSafe(baseDirAbs, d.path, absPath)) {
            CLR_WARN << "[FILE] 路径不安全，已跳过: " << d.path << "\n";
            continue;
        }
        createFiles.emplace_back(d.path, d.content);
    }

    bool filesWereCreated = false;
    if (!createFiles.empty()) {
        auto warnings = detectEmptyBodies(createFiles);
        CLR_INFO << "将要创建的文件:\n";
        for (auto& [p, _] : createFiles) CLR_INFO << "  - " << p << "\n";
        if (!warnings.empty()) {
            CLR_WARN << "\n疑似空实现:\n";
            for (auto& w : warnings) CLR_WARN << "  " << w.file << ":" << w.line << " - " << w.description << "\n";
        }
        if (askUser("继续创建文件？")) {
            for (auto& [relPath, content] : createFiles) {
                std::string native = relPath;
                std::replace(native.begin(), native.end(), '/', '\\');
                std::string fpath = cfg.outDir + "\\" + native;
                size_t sep = fpath.find_last_of('\\');
                if (sep != std::string::npos) makeDirectory(fpath.substr(0, sep));
                if (fileExists(fpath) && !cfg.force) {
                    CLR_WARN << "跳过已存在文件: " << fpath << "\n";
                    continue;
                }
                std::ofstream outFile(fpath, std::ios::binary | std::ios::trunc);
                if (!outFile) { CLR_ERROR << "写入文件出错: " << fpath << "\n"; continue; }
                outFile.write(content.data(), content.size());
                if (outFile.good()) {
                    CLR_SUCCESS << "  + " << fpath << "\n";
                    writeLog(LogLevel::SUCCESS, "[FILE] Created " + fpath);
                    filesWereCreated = true;
                } else {
                    CLR_ERROR << "写入内容出错: " << fpath << "\n";
                }
            }
        } else {
            CLR_INFO << "已跳过文件创建\n";
        }
    }

    if (filesWereCreated && !cfg.noBackup) {
        std::string outDirAbs = fullPath(cfg.outDir);
        if (directoryExists(outDirAbs)) {
            gitBackup(outDirAbs);
            zipBackup(outDirAbs);
        }
    }

    if (cExec > 0) handleExecDirectives(directives, cfg);
    if (cRead > 0) handleReadDirectives(directives, baseDirAbs, cfg);
}