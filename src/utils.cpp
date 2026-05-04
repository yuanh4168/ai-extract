// src/utils.cpp
#include "utils.h"
#include <fstream>
#include <chrono>
#include <iomanip>

static HANDLE safeHandle() {
    static HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    return h;
}

void setColor(Color c) {
    SetConsoleTextAttribute(safeHandle(), static_cast<WORD>(c));
}

void resetColor() {
    SetConsoleTextAttribute(safeHandle(), 7); // 强制恢复白色
}

ColoredOut::ColoredOut(Color c) : m_color(c) {
    setColor(m_color);
}

ColoredOut::~ColoredOut() {
    resetColor();
}

// ★ 全局颜色对象不再定义，颜色输出由 CLR_* 宏（utils.h 中的宏）每次创建临时对象完成

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string toLower(const std::string& s) {
    std::string r(s.size(), '\0');
    std::transform(s.begin(), s.end(), r.begin(), ::tolower);
    return r;
}

std::string getCurrentDir() {
    char buf[MAX_PATH];
    if (_getcwd(buf, sizeof(buf))) return std::string(buf);
    return "unknown";
}

bool setCurrentDir(const std::string& dir) {
    return SetCurrentDirectoryA(dir.c_str()) != 0;
}

void writeLog(LogLevel level, const std::string& msg) {
    static std::ofstream logFile("ai-extract.log", std::ios::app);
    if (!logFile.is_open()) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " [";
    switch (level) {
        case LogLevel::INFO:    logFile << "INFO"; break;
        case LogLevel::WARN:    logFile << "WARN"; break;
        case LogLevel::ERROR:   logFile << "ERROR"; break;
        case LogLevel::SUCCESS: logFile << "SUCCESS"; break;
    }
    logFile << "] " << msg << std::endl;
}