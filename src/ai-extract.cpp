// ai-extract.cpp
// Version with config, colors, window save, runtime commands.
// Build: g++ -std=c++17 -o ai-extract.exe ai-extract.cpp -luser32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

// ----------------------------- color helpers -----------------------
enum class Color {
    WHITE   = 7,   // default console color
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

static void resetColor() {
    setColor(Color::WHITE);
}

class ColoredOut {
    Color m_color;
public:
    explicit ColoredOut(Color c) : m_color(c) { setColor(m_color); }
    ~ColoredOut() { resetColor(); }
    template<typename T>
    ColoredOut& operator<<(const T& val) {
        std::cout << val;
        return *this;
    }
    ColoredOut& operator<<(std::ostream& (*pf)(std::ostream&)) {
        std::cout << pf;
        return *this;
    }
};

#define CLR_INFO    ColoredOut(Color::WHITE)
#define CLR_SUCCESS ColoredOut(Color::GREEN)
#define CLR_WARN    ColoredOut(Color::YELLOW)
#define CLR_ERROR   ColoredOut(Color::RED)
#define CLR_INPUT   ColoredOut(Color::CYAN)

// ----------------------------- config file -------------------------
struct Config {
    std::string fileName = "ai-extract.ini";
    int winLeft = -1, winTop = -1, winWidth = -1, winHeight = -1;
    std::string defaultMode;   // "auto", "loop", "interactive"
    std::string outDir;
    std::string startupDir;    // startup working directory
    bool force = false;
    bool debug = false;
    bool noBackup = false;

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
            if (key == "window_left") winLeft = atoi(val.c_str());
            else if (key == "window_top") winTop = atoi(val.c_str());
            else if (key == "window_width") winWidth = atoi(val.c_str());
            else if (key == "window_height") winHeight = atoi(val.c_str());
            else if (key == "default_mode") defaultMode = val;
            else if (key == "output_dir") outDir = val;
            else if (key == "startup_working_dir") startupDir = val;
            else if (key == "force") force = (val == "1" || val == "true");
            else if (key == "debug") debug = (val == "1" || val == "true");
            else if (key == "no_backup") noBackup = (val == "1" || val == "true");
        }
        return true;
    }

    void save(const std::string& path, int left, int top, int width, int height) const {
        std::ofstream out(path);
        if (!out) return;
        out << "# ai-extract configuration\n";
        out << "window_left=" << left << "\n";
        out << "window_top=" << top << "\n";
        out << "window_width=" << width << "\n";
        out << "window_height=" << height << "\n";
        out << "default_mode=" << defaultMode << "\n";
        out << "output_dir=" << outDir << "\n";
        out << "startup_working_dir=" << startupDir << "\n";
        out << "force=" << (force ? "true" : "false") << "\n";
        out << "debug=" << (debug ? "true" : "false") << "\n";
        out << "no_backup=" << (noBackup ? "true" : "false") << "\n";
    }
};

// ----------------------------- utility -----------------------------
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

static std::string escapeForDebug(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (static_cast<unsigned char>(c) < 0x20)
            result += "\\x" + std::to_string(static_cast<unsigned char>(c));
        else
            result += c;
    }
    return result;
}

// ----------------------------- clipboard ---------------------------
static std::string readClipboard() {
    if (!OpenClipboard(nullptr))
        throw std::runtime_error("Cannot open clipboard");
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == nullptr) {
        CloseClipboard();
        throw std::runtime_error("No text on clipboard");
    }
    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pszText == nullptr) {
        CloseClipboard();
        throw std::runtime_error("Cannot lock clipboard data");
    }
    std::wstring wstr(pszText);
    GlobalUnlock(hData);
    CloseClipboard();

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string utf8(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()),
                        &utf8[0], sizeNeeded, nullptr, nullptr);
    return utf8;
}

// ----------------------------- path helpers (Win32) -----------------
static std::string getCurrentDir() {
    char buf[MAX_PATH];
    if (_getcwd(buf, sizeof(buf)))
        return std::string(buf);
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
            if (!current.empty() && !directoryExists(current)) {
                _mkdir(current.c_str());
            }
        }
    }
}

static std::string fullPath(const std::string& relative) {
    char full[MAX_PATH];
    if (_fullpath(full, relative.c_str(), MAX_PATH))
        return std::string(full);
    return relative;
}

// ----------------------------- parser ------------------------------
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
                    if (trim(contentLines.front()).rfind("```", 0) == 0)
                        contentLines.erase(contentLines.begin());
                    if (!contentLines.empty() && trim(contentLines.back()) == "```")
                        contentLines.pop_back();
                }
                std::string cleaned;
                for (size_t i = 0; i < contentLines.size(); ++i) {
                    if (i > 0) cleaned += '\n';
                    cleaned += contentLines[i];
                }
                files.emplace_back(currentPath, cleaned);
            }
            currentPath = match[1].str();
            if (currentPath.find("..") != std::string::npos || (!currentPath.empty() && currentPath[0] == '/')) {
                CLR_WARN << "跳过不安全路径: " << currentPath << "\n";
                insideFile = false;
                currentContent.str("");
                currentContent.clear();
                continue;
            }
            currentContent.str("");
            currentContent.clear();
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
            if (trim(contentLines.front()).rfind("```", 0) == 0)
                contentLines.erase(contentLines.begin());
            if (!contentLines.empty() && trim(contentLines.back()) == "```")
                contentLines.pop_back();
        }
        std::string cleaned;
        for (size_t i = 0; i < contentLines.size(); ++i) {
            if (i > 0) cleaned += '\n';
            cleaned += contentLines[i];
        }
        files.emplace_back(currentPath, cleaned);
    }
    return files;
}

// --------------------- empty body detection ------------------------
struct Warning {
    std::string file;
    int line;
    std::string description;
};

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
            while (j < static_cast<int>(lines.size()) && lines[j].find_first_not_of(" \t\r\n") == std::string::npos)
                ++j;
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
            while (j < static_cast<int>(lines.size()) && lines[j].find_first_not_of(" \t\r\n") == std::string::npos)
                ++j;
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
        if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
            ext == ".h" || ext == ".hpp" || ext == ".java" || ext == ".cs")
            detectCStyle(path, lines, warnings);
        else if (ext == ".py")
            detectPython(path, lines, warnings);
        else if (ext == ".js" || ext == ".ts" || ext == ".jsx" || ext == ".tsx" || ext == ".mjs")
            detectCStyle(path, lines, warnings);
    }
    return warnings;
}

// ----------------------------- backup ------------------------------
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
    if (rc != 0) {
        CLR_ERROR << "[git] git commit failed\n";
        return false;
    }
    CLR_SUCCESS << "[git] committed.\n";
    return true;
}

static bool zipBackup(const std::string& targetDir) {
    std::string parent = targetDir;
    while (!parent.empty() && (parent.back() == '/' || parent.back() == '\\')) parent.pop_back();
    size_t pos = parent.find_last_of("/\\");
    if (pos != std::string::npos)
        parent = parent.substr(0, pos);
    else
        parent = ".";
    std::string zipName = "project_backup_" + makeTimestamp() + ".zip";
    std::string zipPath = parent + "\\" + zipName;
    std::string cmd = "powershell Compress-Archive -Path \"" + targetDir + "\" -DestinationPath \"" + zipPath + "\" -Force";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        CLR_ERROR << "[zip] 压缩失败\n";
        return false;
    }
    CLR_SUCCESS << "[zip] Archive: " << zipPath << "\n";
    return true;
}

// ----------------------------- file writer -------------------------
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
        if (!outFile) {
            CLR_ERROR << "写入文件出错: " << fpath << "\n";
            anyFailure = true;
            continue;
        }
        outFile.write(content.data(), content.size());
        if (!outFile.good()) {
            CLR_ERROR << "写入内容出错: " << fpath << "\n";
            anyFailure = true;
        } else {
            CLR_SUCCESS << "  + " << fpath << " (" << fullPath(fpath) << ")\n";
        }
    }
    return !anyFailure;
}

// ----------------------------- process one batch -------------------
static void processClipboard(const std::string& sourceText, const std::string& outDir,
                             bool force, bool noBackup, bool interactive = true) {
    auto fileList = parseClipboardText(sourceText);
    if (fileList.empty()) {
        CLR_ERROR << "未找到 ###FILE: 标记\n";
        return;
    }
    CLR_INFO << fileList.size() << " 个文件被找到\n";

    auto warnings = detectEmptyBodies(fileList);
    CLR_INFO << "将要创建的文件:\n";
    for (const auto& [path, _] : fileList)
        CLR_INFO << "  - " << path << "\n";
    if (!warnings.empty()) {
        CLR_WARN << "\n疑似空实现:\n";
        for (const auto& w : warnings)
            CLR_WARN << "  " << w.file << ":" << w.line << " - " << w.description << "\n";
    }

    if (interactive) {
        CLR_INPUT << "\n继续创建文件吗？[y/N] ";
        std::string response;
        std::getline(std::cin, response);
        if (response != "y" && response != "Y") {
            CLR_INFO << "已取消\n";
            return;
        }
    }

    CLR_INFO << "正在创建文件... (输出目录: " << outDir << ")\n";
    if (!writeFiles(fileList, outDir, force)) {
        CLR_ERROR << "部分文件写入失败\n";
    }
    if (!noBackup) {
        gitBackup(outDir);
        zipBackup(outDir);
    }
    CLR_SUCCESS << "批次处理完成\n";
}

// ----------------------------- runtime commands --------------------
static bool handleCommand(const std::string& input, Config& cfg, bool& quit) {
    if (input.empty() || input[0] != ':') return false; // not a command

    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd; // remove colon
    cmd = cmd.substr(1); // strip leading ':'
    std::string arg;
    std::getline(iss, arg);
    arg = trim(arg);

    if (cmd == "help") {
        CLR_INFO << "运行时命令:\n";
        CLR_INFO << "  :help         显示此帮助\n";
        CLR_INFO << "  :dir [path]   显示/改变工作目录\n";
        CLR_INFO << "  :out <dir>    设置输出目录\n";
        CLR_INFO << "  :force [on|off] 强制覆盖开关\n";
        CLR_INFO << "  :debug [on|off] 调试模式开关\n";
        CLR_INFO << "  :backup [on|off] 备份开关\n";
        CLR_INFO << "  :auto         立即处理剪贴板（无确认）\n";
        CLR_INFO << "  :quit         退出程序\n";
    }
    else if (cmd == "dir") {
        if (arg.empty()) {
            CLR_INFO << "当前目录: " << getCurrentDir() << "\n";
        } else {
            if (setCurrentDir(arg)) {
                CLR_SUCCESS << "工作目录已切换到: " << getCurrentDir() << "\n";
                cfg.startupDir = arg; // remember
            } else {
                CLR_ERROR << "无法切换到目录: " << arg << "\n";
            }
        }
    }
    else if (cmd == "out") {
        if (!arg.empty()) {
            cfg.outDir = arg;
            CLR_SUCCESS << "输出目录已设置为: " << cfg.outDir << "\n";
        } else {
            CLR_WARN << "当前输出目录: " << cfg.outDir << "\n";
        }
    }
    else if (cmd == "force") {
        if (arg == "on") cfg.force = true;
        else if (arg == "off") cfg.force = false;
        else cfg.force = !cfg.force; // toggle
        CLR_SUCCESS << "强制覆盖: " << (cfg.force ? "开" : "关") << "\n";
    }
    else if (cmd == "debug") {
        if (arg == "on") cfg.debug = true;
        else if (arg == "off") cfg.debug = false;
        else cfg.debug = !cfg.debug;
        CLR_SUCCESS << "调试模式: " << (cfg.debug ? "开" : "关") << "\n";
    }
    else if (cmd == "backup") {
        if (arg == "on") cfg.noBackup = false;
        else if (arg == "off") cfg.noBackup = true;
        else cfg.noBackup = !cfg.noBackup;
        CLR_SUCCESS << "备份: " << (cfg.noBackup ? "关" : "开") << "\n";
    }
    else if (cmd == "auto") {
        std::string text;
        try {
            text = readClipboard();
            if (text.empty()) {
                CLR_WARN << "剪贴板为空\n";
                return true;
            }
        } catch (const std::exception& e) {
            CLR_ERROR << "读取剪贴板失败: " << e.what() << "\n";
            return true;
        }
        processClipboard(text, cfg.outDir, cfg.force, cfg.noBackup, false);
    }
    else if (cmd == "quit") {
        quit = true;
    }
    else {
        CLR_WARN << "未知命令，输入 :help 查看帮助\n";
    }
    return true; // handled
}

// ----------------------------- window management --------------------
static void applyWindowSettings(const Config& cfg) {
    HWND hwnd = GetConsoleWindow();
    if (hwnd == nullptr) return;
    if (cfg.winLeft >= 0 && cfg.winTop >= 0 && cfg.winWidth > 0 && cfg.winHeight > 0) {
        SetWindowPos(hwnd, nullptr, cfg.winLeft, cfg.winTop, cfg.winWidth, cfg.winHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        CLR_INFO << "窗口位置已恢复: " << cfg.winLeft << "," << cfg.winTop << " " << cfg.winWidth << "x" << cfg.winHeight << "\n";
    }
}

static void saveWindowSettings(Config& cfg) {
    HWND hwnd = GetConsoleWindow();
    if (hwnd == nullptr) return;
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        cfg.winLeft = rect.left;
        cfg.winTop = rect.top;
        cfg.winWidth = rect.right - rect.left;
        cfg.winHeight = rect.bottom - rect.top;
    }
}

// ----------------------------- main --------------------------------
static void printHelp() {
    CLR_INFO << "用法: ai-extract [选项]\n"
              << "  -o <dir>       输出目录\n"
              << "  -f             强制覆盖\n"
              << "  -i <file>      从文件读取\n"
              << "  --no-backup    跳过备份\n"
              << "  --debug        调试模式\n"
              << "  --auto         强制自动模式\n"
              << "  --loop         强制循环模式\n"
              << "  -h, --help     帮助\n"
              << "配置文件: ai-extract.ini\n"
              << "运行时输入 :help 查看命令\n";
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::string configPath = "ai-extract.ini";
    Config cfg;
    bool configLoaded = cfg.load(configPath);

    // Apply window settings before any output
    if (configLoaded) {
        applyWindowSettings(cfg);
    }

    // Apply startup working directory
    if (configLoaded && !cfg.startupDir.empty()) {
        if (setCurrentDir(cfg.startupDir)) {
            CLR_INFO << "初始目录: " << getCurrentDir() << "\n";
        } else {
            CLR_WARN << "启动目录无效: " << cfg.startupDir << "\n";
        }
    }

    // Parse command-line overrides
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
    else if (cfg.outDir.empty()) cfg.outDir = "ai_project";
    if (cmdForce) cfg.force = true;
    if (cmdNoBackup) cfg.noBackup = true;
    if (cmdDebug) cfg.debug = true;

    // If -i is given, process file and exit
    if (!inputFile.empty()) {
        std::ifstream in(inputFile.c_str());
        if (!in) {
            CLR_ERROR << "无法打开文件: " << inputFile << "\n";
            return 1;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        processClipboard(ss.str(), cfg.outDir, cfg.force, cfg.noBackup);
        saveWindowSettings(cfg);
        cfg.save(configPath, cfg.winLeft, cfg.winTop, cfg.winWidth, cfg.winHeight);
        return 0;
    }

    // Mode selection
    std::string mode;
    if (cmdAuto) mode = "auto";
    else if (cmdLoop) mode = "loop";
    else if (configLoaded) mode = cfg.defaultMode;
    else mode = "interactive";

    if (mode == "auto") {
        // Auto mode: read clipboard once, process without confirmation, then exit
        std::string text;
        try {
            text = readClipboard();
            if (text.empty()) {
                CLR_WARN << "剪贴板为空，自动模式无法继续\n";
            } else {
                processClipboard(text, cfg.outDir, cfg.force, cfg.noBackup, false);
            }
        } catch (...) {
            CLR_ERROR << "读取剪贴板失败\n";
        }
    } else {
        // Interactive / loop mode with command support
        CLR_INFO << "=== AI-Extract (交互模式 - 输入 :help 查看命令) ===\n";
        bool quit = false;
        while (!quit) {
            CLR_INPUT << "\n按回车读取剪贴板，或输入命令 (:help) > ";
            std::string userInput;
            std::getline(std::cin, userInput);

            // Check if it's a command
            if (handleCommand(userInput, cfg, quit)) {
                if (quit) break;
                continue;
            }

            // Empty line => proceed to read clipboard
            std::string text;
            try {
                text = readClipboard();
                if (text.empty()) {
                    CLR_WARN << "剪贴板为空\n";
                    continue;
                }
            } catch (const std::exception& e) {
                CLR_ERROR << "读取失败: " << e.what() << "\n";
                continue;
            }

            if (cfg.debug) {
                CLR_INFO << "剪贴板内容:\n" << text << "\n";
            }

            processClipboard(text, cfg.outDir, cfg.force, cfg.noBackup);
        }
    }

    // Save config and window position on exit
    saveWindowSettings(cfg);
    cfg.save(configPath, cfg.winLeft, cfg.winTop, cfg.winWidth, cfg.winHeight);

    CLR_INFO << "程序结束，按回车键退出...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}