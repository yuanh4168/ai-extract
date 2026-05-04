// src/utils.h
#pragma once
#include "common.h"

class ColoredOut {
    Color m_color;
public:
    explicit ColoredOut(Color c);
    ~ColoredOut();
    template<typename T> ColoredOut& operator<<(const T& val) { std::cout << val; return *this; }
    ColoredOut& operator<<(std::ostream& (*pf)(std::ostream&)) { std::cout << pf; return *this; }
};

// 恢复成宏，每次都创建临时对象，析构自动重置颜色
#define CLR_INFO    ColoredOut(Color::WHITE)
#define CLR_SUCCESS ColoredOut(Color::GREEN)
#define CLR_WARN    ColoredOut(Color::YELLOW)
#define CLR_ERROR   ColoredOut(Color::RED)
#define CLR_INPUT   ColoredOut(Color::CYAN)

void setColor(Color c);
void resetColor();

std::string trim(const std::string& s);
std::vector<std::string> splitLines(const std::string& text);
std::string toLower(const std::string& s);
std::string getCurrentDir();
bool setCurrentDir(const std::string& dir);
void writeLog(LogLevel level, const std::string& msg);