#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef ERROR
#undef SUCCESS

#include <shellapi.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
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
#include <cstring>
#include <cctype>
#include <unordered_set>

enum class Color { WHITE=7, GREEN=10, YELLOW=14, RED=12, CYAN=11, MAGENTA=13 };
enum class LogLevel { INFO, WARN, ERROR, SUCCESS };

struct FileDirective {
    enum Type { CREATE_FILE, READ_FILE, DELETE_FILE, EXEC_COMMAND, BROWSE_PAGE };  // 新增 BROWSE_PAGE
    Type type;
    std::string path;
    std::string content;
};

struct Warning { std::string file; int line; std::string description; };

std::string trim(const std::string& s);
std::vector<std::string> splitLines(const std::string& text);
std::string toLower(const std::string& s);
void writeLog(LogLevel level, const std::string& msg);