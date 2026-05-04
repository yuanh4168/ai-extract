// ai-extract.cpp
// 功能：AI代码提取、提示词链、文件读取/删除/执行、备份、目录树、打开文件夹等
// 构建：g++ -std=c++17 -o ai-extract.exe ai-extract.cpp -luser32 -lshell32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <optional>
#include <array>
#include <limits>
#include <direct.h>
#include <functional>

// ----------------------------- 颜色助手 -----------------------------
enum class Color {
    WHITE   = 7,
    GREEN   = 10,
    YELLOW  = 14,
    RED     = 12,
    CYAN    = 11,
    MAGENTA = 13,
};

static void setColor(Color c) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, static_cast<WORD>(c));
}
static void resetColor() { setColor(Color::WHITE); }

class ColoredOut {
    Color m_color;
public:
    explicit ColoredOut(Color c) : m_color(c) { setColor(m_color); }
    ~ColoredOut() { resetColor(); }
    template<typename T> ColoredOut& operator<<(const T& val) { std::cout << val; return *this; }
    ColoredOut& operator<<(std::ostream& (*pf)(std::ostream&)) { std::cout << pf; return *this; }
};

#define CLR_INFO    ColoredOut(Color::WHITE)
#define CLR_SUCCESS ColoredOut(Color::GREEN)
#define CLR_WARN    ColoredOut(Color::YELLOW)
#define CLR_ERROR   ColoredOut(Color::RED)
#define CLR_INPUT   ColoredOut(Color::CYAN)

// ----------------------------- 配置文件 -----------------------------
struct Config {
    std::string fileName = "ai-extract.ini";
    std::string defaultMode;
    std::string outDir;
    std::string startupDir;
    bool force = false;
    bool debug = false;
    bool noBackup = false;
    std::string fileReadMode = "text";
    bool confirmExec = false;

    bool load(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        std::string line;
        while (std::getline(in, line)) {
            size_t pos = line.find('#');
            if (pos != std::string::npos) line.erase(pos);
            pos = line.find_first_not_of(" \t\r\n");
            if (pos == std::string::npos) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            if (key == "default_mode") defaultMode = val;
            else if (key == "output_dir") outDir = val;
            else if (key == "startup_working_dir") startupDir = val;
            else if (key == "force") force = (val == "1" || val == "true");
            else if (key == "debug") debug = (val == "1" || val == "true");
            else if (key == "no_backup") noBackup = (val == "1" || val == "true");
            else if (key == "file_read_mode") fileReadMode = val;
            else if (key == "confirm_exec") confirmExec = (val == "1" || val == "true");
        }
        return true;
    }

    void save(const std::string& path) const {
        std::ofstream out(path);
        if (!out) return;
        out << "# ai-extract configuration\n";
        out << "default_mode=" << defaultMode << "\n";
        out << "output_dir=" << outDir << "\n";
        out << "startup_working_dir=" << startupDir << "\n";
        out << "force=" << (force ? "true" : "false") << "\n";
        out << "debug=" << (debug ? "true" : "false") << "\n";
        out << "no_backup=" << (noBackup ? "true" : "false") << "\n";
        out << "file_read_mode=" << fileReadMode << "\n";
        out << "confirm_exec=" << (confirmExec ? "true" : "false") << "\n";
    }
};

// ----------------------------- 工具函数 -----------------------------
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// ----------------------------- 剪贴板 ---------------------------
static std::string readClipboard() {
    if (!OpenClipboard(nullptr)) throw std::runtime_error("Cannot open clipboard");
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == nullptr) { CloseClipboard(); throw std::runtime_error("No text on clipboard"); }
    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pszText == nullptr) { CloseClipboard(); throw std::runtime_error("Cannot lock clipboard data"); }
    std::wstring wstr(pszText);
    GlobalUnlock(hData);
    CloseClipboard();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &utf8[0], sizeNeeded, nullptr, nullptr);
    return utf8;
}

static void writeClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) { CLR_ERROR << "无法打开剪贴板进行写入\n"; return; }
    EmptyClipboard();
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) { CloseClipboard(); CLR_ERROR << "编码转换失败\n"; return; }
    std::wstring wstr(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wstr[0], wideLen);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wstr.size() + 1) * sizeof(wchar_t));
    if (hMem) {
        wchar_t* pData = static_cast<wchar_t*>(GlobalLock(hMem));
        if (pData) { wcscpy(pData, wstr.c_str()); GlobalUnlock(hMem); SetClipboardData(CF_UNICODETEXT, hMem); }
    }
    CloseClipboard();
    CLR_SUCCESS << "已将内容复制到剪贴板\n";
}

// ----------------------------- 路径辅助 -------------------------
static std::string getCurrentDir() {
    char buf[MAX_PATH];
    if (_getcwd(buf, sizeof(buf))) return std::string(buf);
    return "unknown";
}

static bool setCurrentDir(const std::string& dir) {
    return SetCurrentDirectoryA(dir.c_str()) != 0;
}

static bool directoryExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static bool fileExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static void makeDirectory(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == '\\' || path[i] == '/' || i == path.size() - 1) {
            if (!current.empty() && !directoryExists(current)) _mkdir(current.c_str());
        }
    }
}

static std::string fullPath(const std::string& relative) {
    char full[MAX_PATH];
    if (_fullpath(full, relative.c_str(), MAX_PATH)) return std::string(full);
    return relative;
}

// 可视化目录树
static std::string getDirectoryTree(const std::string& root) {
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

// ----------------------------- 命令执行 -----------------------------
static bool execCommand(const std::string& cmdline, std::string& output, int& exitCode) {
    HANDLE hChildStdoutRd, hChildStdoutWr;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &sa, 0)) {
        output = "创建管道失败";
        return false;
    }
    SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hChildStdoutWr;
    si.hStdOutput = hChildStdoutWr;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::string cmd = "cmd /c \"" + cmdline + " 2>&1\"";

    if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hChildStdoutWr);
        CloseHandle(hChildStdoutRd);
        output = "创建进程失败";
        return false;
    }
    CloseHandle(hChildStdoutWr);

    char buf[256];
    DWORD bytesRead;
    output.clear();
    while (ReadFile(hChildStdoutRd, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
        output.append(buf, bytesRead);
    }
    CloseHandle(hChildStdoutRd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD dwExitCode;
    GetExitCodeProcess(pi.hProcess, &dwExitCode);
    exitCode = static_cast<int>(dwExitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// ----------------------------- 解析器 ------------------------------
static std::vector<std::pair<std::string, std::string>> parseClipboardText(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> files;
    std::regex splitter(R"(^###FILE:\s*([^\r\n]+))");
    auto lines = splitLines(text);
    std::string currentPath;
    std::ostringstream currentContent;
    bool insideFile = false;
    for (const auto& line : lines) {
        std::smatch match;
        std::string trimmedLine = trim(line);
        if (std::regex_match(trimmedLine, match, splitter)) {
            if (insideFile) {
                std::string content = currentContent.str();
                auto contentLines = splitLines(content);
                if (!contentLines.empty()) {
                    if (trim(contentLines.front()).rfind("```", 0) == 0) contentLines.erase(contentLines.begin());
                    if (!contentLines.empty() && trim(contentLines.back()) == "```") contentLines.pop_back();
                }
                std::string cleaned;
                for (size_t i = 0; i < contentLines.size(); ++i) { if (i > 0) cleaned += '\n'; cleaned += contentLines[i]; }
                files.emplace_back(currentPath, cleaned);
            }
            currentPath = match[1].str();
            if (currentPath.find("..") != std::string::npos || (!currentPath.empty() && currentPath[0] == '/')) {
                CLR_WARN << "跳过不安全路径: " << currentPath << "\n";
                insideFile = false;
                currentContent.str(""); currentContent.clear();
                continue;
            }
            currentContent.str(""); currentContent.clear();
            insideFile = true;
        } else if (insideFile) {
            if (currentContent.tellp() > 0) currentContent << '\n';
            currentContent << line;
        }
    }
    if (insideFile) {
        std::string content = currentContent.str();
        auto contentLines = splitLines(content);
        if (!contentLines.empty()) {
            if (trim(contentLines.front()).rfind("```", 0) == 0) contentLines.erase(contentLines.begin());
            if (!contentLines.empty() && trim(contentLines.back()) == "```") contentLines.pop_back();
        }
        std::string cleaned;
        for (size_t i = 0; i < contentLines.size(); ++i) { if (i > 0) cleaned += '\n'; cleaned += contentLines[i]; }
        files.emplace_back(currentPath, cleaned);
    }
    return files;
}

struct FileDirective {
    enum Type { CREATE_FILE, READ_FILE, DELETE_FILE, EXEC_COMMAND };
    Type type;
    std::string path;
    std::string content;
};

static std::vector<FileDirective> parseDirectives(const std::string& text) {
    std::vector<FileDirective> directives;
    std::regex re(R"(^###(FILE|READ|DELETE|EXEC):\s*([^\r\n]*))");
    auto lines = splitLines(text);
    std::string currentType;
    std::string currentPath;
    std::ostringstream currentContent;
    bool insideBlock = false;

    for (const auto& line : lines) {
        std::smatch m;
        std::string trimmed = trim(line);
        if (std::regex_match(trimmed, m, re)) {
            if (insideBlock) {
                std::string content = currentContent.str();
                if (currentType == "FILE" || currentType == "EXEC") {
                    auto clines = splitLines(content);
                    if (!clines.empty() && trim(clines.front()).rfind("```", 0) == 0) clines.erase(clines.begin());
                    if (!clines.empty() && trim(clines.back()) == "```") clines.pop_back();
                    std::string cleaned;
                    for (size_t i = 0; i < clines.size(); ++i) { if (i > 0) cleaned += '\n'; cleaned += clines[i]; }
                    content = cleaned;
                }
                FileDirective::Type t = FileDirective::CREATE_FILE;
                if (currentType == "FILE") t = FileDirective::CREATE_FILE;
                else if (currentType == "READ") t = FileDirective::READ_FILE;
                else if (currentType == "DELETE") t = FileDirective::DELETE_FILE;
                else if (currentType == "EXEC") t = FileDirective::EXEC_COMMAND;
                if (t == FileDirective::EXEC_COMMAND)
                    directives.push_back({t, "", content});
                else
                    directives.push_back({t, currentPath, content});
            }
            currentType = m[1].str();
            currentPath = m[2].str();
            currentContent.str(""); currentContent.clear();
            insideBlock = true;
        } else if (insideBlock) {
            if (currentContent.tellp() > 0) currentContent << '\n';
            currentContent << line;
        }
    }
    if (insideBlock) {
        std::string content = currentContent.str();
        if (currentType == "FILE" || currentType == "EXEC") {
            auto clines = splitLines(content);
            if (!clines.empty() && trim(clines.front()).rfind("```", 0) == 0) clines.erase(clines.begin());
            if (!clines.empty() && trim(clines.back()) == "```") clines.pop_back();
            std::string cleaned;
            for (size_t i = 0; i < clines.size(); ++i) { if (i > 0) cleaned += '\n'; cleaned += clines[i]; }
            content = cleaned;
        }
        FileDirective::Type t = FileDirective::CREATE_FILE;
        if (currentType == "FILE") t = FileDirective::CREATE_FILE;
        else if (currentType == "READ") t = FileDirective::READ_FILE;
        else if (currentType == "DELETE") t = FileDirective::DELETE_FILE;
        else if (currentType == "EXEC") t = FileDirective::EXEC_COMMAND;
        if (t == FileDirective::EXEC_COMMAND)
            directives.push_back({t, "", content});
        else
            directives.push_back({t, currentPath, content});
    }
    return directives;
}

// --------------------- 空函数检测 ------------------------
struct Warning { std::string file; int line; std::string description; };

static void detectCStyle(const std::string& file, const std::vector<std::string>& lines,
                         std::vector<Warning>& warnings) {
    std::regex emptyBodyRe(R"(\)\s*\{\s*\})");
    std::regex returnBodyRe(R"(\)\s*\{\s*return\s*;\s*\})");
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const std::string& line = lines[i];
        if (std::regex_search(line, emptyBodyRe))
            warnings.push_back({file, i + 1, trim(line) + " (empty body)"});
        else if (std::regex_search(line, returnBodyRe))
            warnings.push_back({file, i + 1, trim(line) + " (empty body, return;)"});
        else if (std::regex_search(line, std::regex(R"(\)\s*\{)")) && line.find('}') == std::string::npos) {
            int j = i + 1;
            while (j < static_cast<int>(lines.size()) && lines[j].find_first_not_of(" \t\r\n") == std::string::npos) ++j;
            if (j < static_cast<int>(lines.size()) && trim(lines[j]) == "}")
                warnings.push_back({file, i + 1, trim(line) + " ... } (empty body)"});
        }
    }
}

static void detectPython(const std::string& file, const std::vector<std::string>& lines,
                         std::vector<Warning>& warnings) {
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const std::string& line = lines[i];
        std::string t = trim(line);
        if ((t.rfind("def ", 0) == 0 || t.rfind("async def ", 0) == 0) && t.back() == ':') {
            size_t indent = line.find_first_not_of(" \t");
            if (indent == std::string::npos) indent = 0;
            int j = i + 1;
            while (j < static_cast<int>(lines.size()) && lines[j].find_first_not_of(" \t\r\n") == std::string::npos) ++j;
            if (j < static_cast<int>(lines.size())) {
                std::string nextLine = lines[j];
                std::string nextTrim = trim(nextLine);
                size_t nextIndent = nextLine.find_first_not_of(" \t");
                if (nextIndent == std::string::npos) nextIndent = 0;
                if (nextIndent > indent && (nextTrim == "pass" || nextTrim == "..."))
                    warnings.push_back({file, i + 1, t + " (" + nextTrim + ")"});
            }
        }
    }
}

static std::vector<Warning> detectEmptyBodies(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<Warning> warnings;
    for (const auto& [path, content] : files) {
        auto lines = splitLines(content);
        std::string ext;
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) ext = path.substr(dot);
        if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext == ".java" || ext == ".cs")
            detectCStyle(path, lines, warnings);
        else if (ext == ".py") detectPython(path, lines, warnings);
        else if (ext == ".js" || ext == ".ts" || ext == ".jsx" || ext == ".tsx" || ext == ".mjs")
            detectCStyle(path, lines, warnings);
    }
    return warnings;
}

// ----------------------------- 备份 ------------------------------
static std::string makeTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

static bool gitBackup(const std::string& targetDir) {
    std::string oldDir = getCurrentDir();
    if (!SetCurrentDirectoryA(targetDir.c_str())) { CLR_ERROR << "[git] 无法进入目录 " << targetDir << "\n"; return false; }
    if (!directoryExists(".git")) {
        if (std::system("git init") != 0) { CLR_ERROR << "[git] git init failed\n"; SetCurrentDirectoryA(oldDir.c_str()); return false; }
    }
    if (std::system("git add -A") != 0) { CLR_ERROR << "[git] git add failed\n"; SetCurrentDirectoryA(oldDir.c_str()); return false; }
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss; oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    std::string msg = "auto backup at " + oss.str();
    std::string cmd = "git -c user.name=ai-extract -c user.email=ai-extract@local commit -m \"" + msg + "\"";
    int rc = std::system(cmd.c_str());
    SetCurrentDirectoryA(oldDir.c_str());
    if (rc != 0) { CLR_ERROR << "[git] git commit failed\n"; return false; }
    CLR_SUCCESS << "[git] committed.\n";
    return true;
}

static bool zipBackup(const std::string& targetDir) {
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

// ----------------------------- 文件写入 -------------------------
static bool writeFiles(const std::vector<std::pair<std::string, std::string>>& files,
                       const std::string& outDir, bool force) {
    bool anyFailure = false;
    for (const auto& [relPath, content] : files) {
        std::string nativeRel = relPath;
        std::replace(nativeRel.begin(), nativeRel.end(), '/', '\\');
        std::string fpath = outDir + "\\" + nativeRel;
        size_t lastSep = fpath.find_last_of('\\');
        if (lastSep != std::string::npos) {
            std::string parent = fpath.substr(0, lastSep);
            makeDirectory(parent);
        }
        if (fileExists(fpath) && !force) {
            CLR_WARN << "跳过已存在的文件: " << fpath << " (使用 -f 覆盖)\n";
            continue;
        }
        std::ofstream outFile(fpath, std::ios::binary | std::ios::trunc);
        if (!outFile) { CLR_ERROR << "写入文件出错: " << fpath << "\n"; anyFailure = true; continue; }
        outFile.write(content.data(), content.size());
        if (!outFile.good()) { CLR_ERROR << "写入内容出错: " << fpath << "\n"; anyFailure = true; }
        else { CLR_SUCCESS << "  + " << fpath << " (" << fullPath(fpath) << ")\n"; }
    }
    return !anyFailure;
}

// ----------------------------- 指令处理 -------------------------
static bool askUser(const std::string& question) {
    CLR_INPUT << question << " [y/N] ";
    std::string ans;
    std::getline(std::cin, ans);
    return ans == "y" || ans == "Y";
}

static void handleReadDirectives(const std::vector<FileDirective>& directives, const std::string& baseDir, const Config& cfg) {
    for (const auto& d : directives) {
        if (d.type != FileDirective::READ_FILE) continue;
        std::string nativePath = d.path;
        std::replace(nativePath.begin(), nativePath.end(), '/', '\\');
        std::string full = fullPath(baseDir + "\\" + nativePath);
        if (!fileExists(full)) { CLR_WARN << "[READ] 文件不存在: " << full << "\n"; continue; }
        if (cfg.fileReadMode == "path") {
            writeClipboard(full);
            CLR_INFO << "文件路径已复制到剪贴板，请发送给 AI: " << full << "\n";
        } else {
            std::ifstream in(full, std::ios::binary);
            if (!in) { CLR_ERROR << "[READ] 无法打开文件: " << full << "\n"; continue; }
            std::ostringstream oss; oss << in.rdbuf();
            std::string content = oss.str();
            writeClipboard(content);
            CLR_INFO << "文件内容 (" << content.size() << " 字节) 已复制到剪贴板，请发送给 AI\n";
        }
    }
}

static void handleDeleteDirectives(const std::vector<FileDirective>& directives, const std::string& baseDir) {
    bool anyDelete = false;
    for (const auto& d : directives) if (d.type == FileDirective::DELETE_FILE) { anyDelete = true; break; }
    if (!anyDelete) return;
    CLR_WARN << "\n检测到文件删除指令。即将删除以下文件:\n";
    for (const auto& d : directives) {
        if (d.type == FileDirective::DELETE_FILE) {
            std::string nativePath = d.path;
            std::replace(nativePath.begin(), nativePath.end(), '/', '\\');
            CLR_WARN << "  - " << fullPath(baseDir + "\\" + nativePath) << "\n";
        }
    }
    if (!askUser("确认要删除以上文件吗？(第一次确认)")) { CLR_INFO << "取消删除操作。\n"; return; }
    if (!askUser("请再次确认：真的要永久删除以上文件吗？(第二次确认)")) { CLR_INFO << "取消删除操作。\n"; return; }
    for (const auto& d : directives) {
        if (d.type != FileDirective::DELETE_FILE) continue;
        std::string nativePath = d.path;
        std::replace(nativePath.begin(), nativePath.end(), '/', '\\');
        std::string full = fullPath(baseDir + "\\" + nativePath);
        if (DeleteFileA(full.c_str())) { CLR_SUCCESS << "[DELETE] 已删除: " << full << "\n"; }
        else { CLR_ERROR << "[DELETE] 删除失败: " << full << "\n"; }
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
            if (ans != "y" && ans != "Y") {
                CLR_INFO << "已跳过命令执行\n";
                continue;
            }
        }
        CLR_INFO << "[EXEC] 执行: " << cmd << "\n";
        std::string output;
        int exitCode = -1;
        if (execCommand(cmd, output, exitCode)) {
            if (exitCode == 0) {
                CLR_SUCCESS << "[EXEC] 命令执行成功 (退出码 0)\n";
                if (!output.empty()) {
                    CLR_INFO << "输出:\n" << output;
                }
            } else {
                CLR_ERROR << "[EXEC] 命令执行失败 (退出码 " << exitCode << ")\n";
                if (!output.empty()) {
                    CLR_ERROR << "输出/错误:\n" << output;
                }
            }
            writeClipboard(output);
        } else {
            CLR_ERROR << "[EXEC] 无法执行命令\n";
        }
    }
}

// ★ 调整处理顺序：删除 → 创建 → 执行 → 读取
static void processDirectives(const std::vector<FileDirective>& directives, const Config& cfg) {
    std::string baseDirAbs = fullPath(cfg.outDir);
    int createCount = 0, readCount = 0, deleteCount = 0, execCount = 0;
    for (auto& d : directives) {
        if (d.type == FileDirective::CREATE_FILE) createCount++;
        else if (d.type == FileDirective::READ_FILE) readCount++;
        else if (d.type == FileDirective::DELETE_FILE) deleteCount++;
        else if (d.type == FileDirective::EXEC_COMMAND) execCount++;
    }
    if (createCount + readCount + deleteCount + execCount == 0) return;

    CLR_INFO << "指令统计: 创建 " << createCount << " 个, 读取 " << readCount
              << " 个, 删除 " << deleteCount << " 个, 执行 " << execCount << " 个命令\n";

    // 1. 删除
    if (deleteCount > 0) handleDeleteDirectives(directives, baseDirAbs);

    // 2. 创建文件
    std::vector<std::pair<std::string, std::string>> createFiles;
    for (auto& d : directives)
        if (d.type == FileDirective::CREATE_FILE) createFiles.emplace_back(d.path, d.content);

    if (!createFiles.empty()) {
        auto warnings = detectEmptyBodies(createFiles);
        CLR_INFO << "将要创建的文件:\n";
        for (auto& [path, _] : createFiles) CLR_INFO << "  - " << path << "\n";
        if (!warnings.empty()) {
            CLR_WARN << "\n疑似空实现:\n";
            for (auto& w : warnings) CLR_WARN << "  " << w.file << ":" << w.line << " - " << w.description << "\n";
        }
        if (askUser("继续创建文件吗？")) {
            CLR_INFO << "正在创建...\n";
            if (!writeFiles(createFiles, cfg.outDir, cfg.force))
                CLR_ERROR << "部分文件写入失败\n";
        } else {
            CLR_INFO << "已跳过文件创建\n";
        }
    }

    // 3. 执行命令（此时文件已存在）
    if (execCount > 0) handleExecDirectives(directives, cfg);

    // 4. 读取文件（可选）
    if (readCount > 0) handleReadDirectives(directives, baseDirAbs, cfg);
}

// ----------------------------- 提示词链模式 ------------------------
static const std::string DEFAULT_PROMPT_TEMPLATE = R"(
你是一个严格遵循输出格式的代码生成与文件操作助手。你只能输出两类内容：回答用户问题和输出代码，每一次回答的正文部分只能包含上面的这两类别的其中一个，回答用户问题正常的回答，给出代码的部分必须且只能使用下述四种指令来响应，不能添加任何解释、说明、问候或 Markdown 装饰。整个回复由指令序列构成，指令之间由空行分隔。

## 指令语法

### 1. 创建/更新文件
```
###FILE: 相对路径/文件名
文件完整内容（可多行）
```

- 路径使用正斜杠 `/`，相对于项目根目录。
- 路径不得包含 `..` 或以 `/` 开头（绝对路径），否则会被拒绝。
- 文件内容原样保留，包括缩进和空行。如果内容中包含三反引号，直接书写即可，工具会自动处理。

### 2. 读取文件内容到剪贴板
```
###READ: 相对路径/文件名
```

- 工具会将对应文件的内容（或路径，取决于配置）复制到剪贴板，方便用户传给 AI。

### 3. 删除文件
```
###DELETE: 相对路径/文件名
```

- 工具会要求用户两次确认后才执行删除。

### 4. 执行命令行命令
```
###EXEC:
命令行指令（可多行，但通常一行即可）
```

- 指令从 `###EXEC:` 后换行开始，直到下一个指令头或文件结束。
- 常用于编译、运行代码。例如：

```
###EXEC:
g++ -std=c++17 -o program src/main.cpp && ./program
```

- 工具会执行该命令，并将标准输出和标准错误合并后复制到剪贴板。退出码非零时会显示警告。

## 格式约束

- 每条指令必须以 `###FILE:`、`###READ:`、`###DELETE:` 或 `###EXEC:` 开头，且冒号后有一个空格（对于 FILE/READ/DELETE）或直接换行（对于 EXEC）。
- 指令体结束后，下一个指令头或回复结束即为该指令的终止。
- 如果你的回复中需要包含多个文件，请连续排列多个 `###FILE:` 指令。
- **整个回复中不能出现任何自然语言文本、解释、问候、补充说明或 Markdown 代码块标记（如 ```）来包裹指令。**
- 你可以建议用户将 `###EXEC` 放在 `###FILE` 之后，以确保文件已创建再执行。

## 示例（正确的输出格式）

```
###FILE: src/main.cpp
#include <iostream>
int main() {
    std::cout << "Hello";
    return 0;
}

###FILE: README.md
# My Project

###EXEC:
g++ -std=c++17 -o hello src/main.cpp && hello

###READ: src/main.cpp
```
注意：以上示例中没有额外文字，这就是整个回复的全部内容。

请严格遵循上述规则，根据用户需求生成对应的指令序列。

当前需求：
)";

static void promptChainMode(Config& cfg) {
    std::vector<std::string> requirements;
    CLR_INFO << "=== 提示词链模式 ===\n";
    CLR_INFO << "默认提示词模板:\n" << DEFAULT_PROMPT_TEMPLATE << "\n";
    CLR_INFO << "请输入你的第一条需求，或输入 :help 查看命令\n";

    while (true) {
        CLR_INPUT << "需求> ";
        std::string input;
        std::getline(std::cin, input);
        input = trim(input);
        if (input.empty()) continue;
        if (input[0] == ':') {
            std::string cmd = input.substr(1);
            if (cmd == "help") {
                CLR_INFO << "命令: :help :history :undo :reset :generate :tree :quit/:exit\n";
            } else if (cmd == "history") {
                if (requirements.empty()) CLR_INFO << "暂无需求\n";
                else for (size_t i = 0; i < requirements.size(); ++i) CLR_INFO << "  " << i+1 << ". " << requirements[i] << "\n";
            } else if (cmd == "undo") {
                if (requirements.empty()) CLR_WARN << "没有可撤销的需求\n";
                else { requirements.pop_back(); CLR_SUCCESS << "已撤销最后一条需求\n"; }
            } else if (cmd == "reset") {
                requirements.clear(); CLR_SUCCESS << "需求已清空\n";
            } else if (cmd == "generate") {
                std::ostringstream prompt;
                prompt << DEFAULT_PROMPT_TEMPLATE;
                for (auto& req : requirements) prompt << req << "\n";
                std::string tree = getDirectoryTree(fullPath(cfg.outDir));
                if (!tree.empty()) {
                    prompt << "\n当前项目的文件结构如下（供参考）：\n" << tree;
                }
                writeClipboard(prompt.str());
            } else if (cmd == "tree") {
                std::string tree = getDirectoryTree(fullPath(cfg.outDir));
                if (tree.empty()) {
                    CLR_INFO << "输出目录为空或不存在。\n";
                } else {
                    CLR_INFO << "当前项目文件结构:\n" << tree;
                    std::string treeEntry = "当前项目的文件结构（参考）：\n" + tree;
                    requirements.push_back(treeEntry);
                    CLR_SUCCESS << "已将项目结构添加为需求 (" << requirements.size() << ")\n";
                }
            } else if (cmd == "quit" || cmd == "exit") {
                CLR_INFO << "退出提示词模式\n"; break;
            } else CLR_WARN << "未知命令: " << cmd << "\n";
        } else {
            requirements.push_back(input);
            CLR_SUCCESS << "已添加需求 (" << requirements.size() << "): " << input << "\n";
        }
    }
}

// ----------------------------- 主界面命令处理 --------------------
static void printHelp() {
    CLR_INFO << "用法: ai-extract [选项]\n"
              << "  -o <dir>       输出目录 (默认: .)\n"
              << "  -f             强制覆盖\n"
              << "  -i <file>      从文本文件读取\n"
              << "  --no-backup    跳过备份\n"
              << "  --debug        调试模式\n"
              << "  --auto         自动模式\n"
              << "  --loop         循环模式\n"
              << "  -h, --help     帮助\n";
}

static void handleCommand(const std::string& input, Config& cfg, bool& quit, bool& promptMode, bool& autoProcess) {
    if (input.empty() || input[0] != ':') return;
    std::istringstream iss(input);
    std::string cmd; iss >> cmd; cmd = cmd.substr(1);
    std::string arg; std::getline(iss, arg); arg = trim(arg);

    if (cmd == "help") {
        CLR_INFO << "命令: :help :dir :out :force :debug :backup :auto :prompt :tree :open :quit\n";
    } else if (cmd == "dir") {
        if (arg.empty()) CLR_INFO << "当前目录: " << getCurrentDir() << "\n";
        else {
            if (setCurrentDir(arg)) { CLR_SUCCESS << "工作目录已切换到: " << getCurrentDir() << "\n"; cfg.startupDir = arg; }
            else CLR_ERROR << "无法切换到目录: " << arg << "\n";
        }
    } else if (cmd == "out") {
        if (!arg.empty()) { cfg.outDir = arg; CLR_SUCCESS << "输出目录已设置为: " << fullPath(cfg.outDir) << "\n"; }
        else CLR_WARN << "当前输出目录: " << fullPath(cfg.outDir) << "\n";
    } else if (cmd == "force") {
        if (arg == "on") cfg.force = true; else if (arg == "off") cfg.force = false; else cfg.force = !cfg.force;
        CLR_SUCCESS << "强制覆盖: " << (cfg.force ? "开" : "关") << "\n";
    } else if (cmd == "debug") {
        if (arg == "on") cfg.debug = true; else if (arg == "off") cfg.debug = false; else cfg.debug = !cfg.debug;
        CLR_SUCCESS << "调试模式: " << (cfg.debug ? "开" : "关") << "\n";
    } else if (cmd == "backup") {
        if (arg == "on") cfg.noBackup = false; else if (arg == "off") cfg.noBackup = true; else cfg.noBackup = !cfg.noBackup;
        CLR_SUCCESS << "备份: " << (cfg.noBackup ? "关" : "开") << "\n";
    } else if (cmd == "auto") autoProcess = true;
    else if (cmd == "prompt") promptMode = true;
    else if (cmd == "tree") {
        std::string tree = getDirectoryTree(fullPath(cfg.outDir));
        if (tree.empty()) CLR_INFO << "输出目录为空或不存在。\n";
        else {
            CLR_INFO << "当前项目文件结构:\n" << tree;
            writeClipboard(tree);
        }
    } else if (cmd == "open") {
        std::string path = fullPath(cfg.outDir);
        ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        CLR_SUCCESS << "已打开文件夹: " << path << "\n";
    } else if (cmd == "quit") quit = true;
    else CLR_WARN << "未知命令: " << cmd << "\n";
}

// ----------------------------- main --------------------------------
int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::string configPath = "ai-extract.ini";
    Config cfg;
    bool configLoaded = cfg.load(configPath);

    if (configLoaded && !cfg.startupDir.empty()) {
        if (setCurrentDir(cfg.startupDir)) CLR_INFO << "初始目录: " << getCurrentDir() << "\n";
    }

    bool cmdForce = false, cmdNoBackup = false, cmdDebug = false, cmdAuto = false, cmdLoop = false;
    std::string cmdOutDir;
    std::string inputFile;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i+1 < argc) cmdOutDir = argv[++i];
        else if (arg == "-f") cmdForce = true;
        else if (arg == "--no-backup") cmdNoBackup = true;
        else if (arg == "--debug") cmdDebug = true;
        else if (arg == "--auto") cmdAuto = true;
        else if (arg == "--loop") cmdLoop = true;
        else if (arg == "-i" && i+1 < argc) inputFile = argv[++i];
        else if (arg == "-h" || arg == "--help") { printHelp(); return 0; }
        else { CLR_ERROR << "未知选项: " << arg << "\n"; return 1; }
    }

    if (!cmdOutDir.empty()) cfg.outDir = cmdOutDir;
    if (cfg.outDir.empty()) cfg.outDir = ".";
    if (cmdForce) cfg.force = true;
    if (cmdNoBackup) cfg.noBackup = true;
    if (cmdDebug) cfg.debug = true;

    if (!inputFile.empty()) {
        std::ifstream in(inputFile.c_str());
        if (!in) { CLR_ERROR << "无法打开文件: " << inputFile << "\n"; return 1; }
        std::ostringstream ss; ss << in.rdbuf();
        auto directives = parseDirectives(ss.str());
        processDirectives(directives, cfg);
        cfg.save(configPath);
        return 0;
    }

    std::string mode;
    if (cmdAuto) mode = "auto";
    else if (cmdLoop) mode = "loop";
    else if (configLoaded) mode = cfg.defaultMode;
    else mode = "interactive";

    if (mode == "auto") {
        std::string text;
        try { text = readClipboard(); } catch (...) { CLR_ERROR << "读取剪贴板失败\n"; }
        if (!text.empty()) {
            auto directives = parseDirectives(text);
            processDirectives(directives, cfg);
        } else CLR_WARN << "剪贴板为空\n";
    } else {
        bool quit = false;
        while (!quit) {
            CLR_INPUT << "\n按回车读取剪贴板，或输入命令 (:help) > ";
            std::string userInput;
            std::getline(std::cin, userInput);
            userInput = trim(userInput);

            if (userInput.empty()) {
                std::string text;
                try { text = readClipboard(); } catch (const std::exception& e) { CLR_ERROR << "读取失败: " << e.what() << "\n"; continue; }
                if (text.empty()) { CLR_WARN << "剪贴板为空\n"; continue; }
                if (cfg.debug) CLR_INFO << "剪贴板内容:\n" << text << "\n";
                auto directives = parseDirectives(text);
                processDirectives(directives, cfg);
                continue;
            }

            bool promptMode = false, autoProcess = false;
            handleCommand(userInput, cfg, quit, promptMode, autoProcess);

            if (promptMode) promptChainMode(cfg);
            if (autoProcess) {
                std::string text;
                try { text = readClipboard(); } catch (...) { CLR_ERROR << "读取失败\n"; }
                if (!text.empty()) {
                    auto directives = parseDirectives(text);
                    processDirectives(directives, cfg);
                }
            }
        }
    }

    cfg.save(configPath);
    CLR_INFO << "程序结束，按回车键退出...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}