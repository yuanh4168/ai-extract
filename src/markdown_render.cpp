// src/markdown_render.cpp
#include "markdown_render.h"
#include <regex>
#include <sstream>
#include <vector>
#include <algorithm>

// ==================== ANSI 样式常量 ====================
static const char* RESET      = "\033[0m";
static const char* BOLD       = "\033[1m";
static const char* ITALIC     = "\033[3m";
static const char* BOLD_ITALIC= "\033[1;3m";
static const char* REVERSE    = "\033[7m";
static const char* CYAN       = "\033[36m";
static const char* BLUE       = "\033[34m";
static const char* YELLOW     = "\033[33m";
static const char* GREEN      = "\033[32m";
static const char* MAGENTA    = "\033[35m";
static const char* WHITE      = "\033[37m";
static const char* GRAY       = "\033[90m";
static const char* BORDER_COLOR = "\033[36m";   // 边框青色

static void append(std::string& str, const char* ansistr) {
    str += ansistr;
}

// 去除 ANSI 转义序列
static std::string stripAnsi(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    bool inEscape = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (inEscape) {
            if (s[i] == 'm') inEscape = false;
            continue;
        }
        if (s[i] == '\033') {
            inEscape = true;
            continue;
        }
        result += s[i];
    }
    return result;
}

// 手动解析 UTF-8 序列，返回码点（最多处理 4 字节）
static int decodeUtf8(const char*& p) {
    unsigned char c = *p++;
    int cp = c;
    if (c <= 0x7F) return cp;               // 1 字节
    int len = 0;
    if ((c & 0xE0) == 0xC0) len = 1, cp = c & 0x1F;
    else if ((c & 0xF0) == 0xE0) len = 2, cp = c & 0x0F;
    else if ((c & 0xF8) == 0xF0) len = 3, cp = c & 0x07;
    else return -1;                         // 非法字节
    for (int i = 0; i < len; ++i) {
        c = *p++;
        if ((c & 0xC0) != 0x80) return -1; // 非法后续字节
        cp = (cp << 6) | (c & 0x3F);
    }
    return cp;
}

// 计算 UTF-8 字符串在等宽终端中的显示宽度（CJK 字符宽度视为 2）
static size_t displayWidth(const std::string& utf8) {
    size_t width = 0;
    const char* p = utf8.c_str();
    const char* end = p + utf8.size();
    while (p < end) {
        int cp = decodeUtf8(p);
        if (cp < 0) { ++width; continue; }  // 非法字节当作半角
        if ((cp >= 0x4E00 && cp <= 0x9FFF) ||
            (cp >= 0x3400 && cp <= 0x4DBF) ||
            (cp >= 0xF900 && cp <= 0xFAFF) ||
            (cp >= 0x2000 && cp <= 0x206F) ||   // 广义标点
            cp == 0x3000 || cp == 0x2014 || cp == 0x2018 || cp == 0x2019 ||
            cp == 0x201C || cp == 0x201D || cp == 0x2026 || cp == 0x00B7)
            width += 2;
        else
            width += 1;
    }
    return width;
}

// 带样式的字符串的纯文本显示宽度
static size_t displayWidthPlain(const std::string& styled) {
    return displayWidth(stripAnsi(styled));
}

// ==================== 行内格式渲染 ====================
static std::string renderInlines(const std::string& line) {
    std::string result;
    result.reserve(line.size() * 2);
    for (size_t i = 0; i < line.size(); ) {
        // 行内代码
        if (line[i] == '`') {
            size_t end = line.find('`', i + 1);
            if (end != std::string::npos) {
                append(result, GREEN);
                result += line.substr(i, end - i + 1);
                append(result, RESET);
                i = end + 1;
                continue;
            }
        }
        // 粗斜体 ***
        if (i + 2 < line.size() && line.substr(i, 3) == "***") {
            size_t end = line.find("***", i + 3);
            if (end != std::string::npos) {
                append(result, BOLD_ITALIC);
                result += line.substr(i + 3, end - i - 3);
                append(result, RESET);
                i = end + 3;
                continue;
            }
        }
        // 粗体 **
        if (i + 1 < line.size() && line.substr(i, 2) == "**") {
            size_t end = line.find("**", i + 2);
            if (end != std::string::npos) {
                append(result, BOLD);
                result += line.substr(i + 2, end - i - 2);
                append(result, RESET);
                i = end + 2;
                continue;
            }
        }
        // 斜体 *
        if (line[i] == '*' && !(i > 0 && line[i-1] == '*')) {
            size_t end = line.find('*', i + 1);
            if (end != std::string::npos &&
                (end + 1 >= line.size() || line[end+1] != '*')) {
                if (i == 0 && (end - i > 1 && line[i+1] == ' ')) {
                    result += line[i++];
                    continue;
                }
                append(result, ITALIC);
                result += line.substr(i + 1, end - i - 1);
                append(result, RESET);
                i = end + 1;
                continue;
            }
        }
        result += line[i++];
    }
    return result;
}

// ==================== 表格 ====================
static bool isTableSeparator(const std::string& line) {
    return std::regex_match(line,
        std::regex(R"(^\s*\|[\s\-:]+\|[\s\-:]+\|?\s*$)"));
}

static std::vector<std::string> parseTableRow(const std::string& line) {
    std::vector<std::string> cells;
    std::string trimmed = line;
    if (!trimmed.empty() && trimmed.front() == '|') trimmed.erase(0, 1);
    if (!trimmed.empty() && trimmed.back() == '|') trimmed.pop_back();
    std::string cell;
    std::istringstream stream(trimmed);
    while (std::getline(stream, cell, '|')) {
        size_t start = cell.find_first_not_of(" \t");
        size_t end   = cell.find_last_not_of(" \t");
        if (start != std::string::npos)
            cells.push_back(cell.substr(start, end - start + 1));
        else
            cells.push_back("");
    }
    return cells;
}

static std::string formatTable(const std::vector<std::string>& rawLines) {
    if (rawLines.size() < 2) return "";
    std::vector<std::vector<std::string>> rows;
    for (const auto& line : rawLines) {
        if (!isTableSeparator(line))
            rows.push_back(parseTableRow(line));
    }
    if (rows.empty()) return "";

    size_t cols = 0;
    for (const auto& r : rows) cols = std::max(cols, r.size());

    std::vector<size_t> colWidths(cols, 0);
    for (const auto& r : rows) {
        for (size_t j = 0; j < r.size(); ++j) {
            std::string styled = renderInlines(r[j]);
            size_t w = displayWidthPlain(styled);
            colWidths[j] = std::max(colWidths[j], w);
        }
    }

    std::string result;
    auto drawSeparator = [&](char left, char mid, char right, char fill) {
        result += std::string(BORDER_COLOR);
        result += left;
        for (size_t j = 0; j < cols; ++j) {
            if (j > 0) result += mid;
            result += std::string(colWidths[j] + 2, fill);
        }
        result += right;
        result += std::string(RESET) + "\n";
    };

    drawSeparator('+', '+', '+', '-');
    for (size_t r = 0; r < rows.size(); ++r) {
        result += std::string(BORDER_COLOR) + "|" + RESET;
        for (size_t j = 0; j < cols; ++j) {
            std::string raw = (j < rows[r].size()) ? rows[r][j] : "";
            std::string styled = renderInlines(raw);
            size_t plainWidth = displayWidthPlain(styled);
            size_t pad = colWidths[j] - plainWidth + 1;
            result += " " + styled + std::string(pad, ' ');
            result += std::string(BORDER_COLOR) + "|" + RESET;
        }
        result += "\n";
        if (r == 0) drawSeparator('+', '+', '+', '-');
    }
    drawSeparator('+', '+', '+', '-');
    return result;
}

// ==================== 代码块 ====================
static std::string formatCodeBlock(const std::vector<std::string>& codeLines,
                                   const std::string& lang) {
    if (codeLines.empty()) return "";
    size_t maxWidth = 0;
    for (const auto& line : codeLines)
        maxWidth = std::max(maxWidth, displayWidth(line));

    std::string title = lang.empty() ? " code " : " " + lang + " ";
    size_t titleWidth = displayWidth(title);
    std::string topBorder;
    size_t frameWidth = maxWidth + 4;  // "| " + text + " |"
    if (titleWidth <= frameWidth) {
        size_t leftDash  = (frameWidth - titleWidth) / 2;
        size_t rightDash = frameWidth - titleWidth - leftDash;
        topBorder = std::string(BORDER_COLOR) + "+" + std::string(leftDash, '-') + title + std::string(rightDash, '-') + "+" + RESET + "\n";
    } else {
        topBorder = std::string(BORDER_COLOR) + "+" + title + std::string(2, '-') + "+" + RESET + "\n";
    }

    std::string bottomBorder;
    bottomBorder = std::string(BORDER_COLOR) + "+" + std::string(frameWidth, '-') + "+" + RESET + "\n";

    std::string result = topBorder;
    for (const auto& line : codeLines) {
        result += std::string(BORDER_COLOR) + "| " + RESET + WHITE + line + RESET;
        size_t pad = maxWidth - displayWidth(line);
        if (pad > 0) result += std::string(pad, ' ');
        result += std::string(BORDER_COLOR) + " |" + RESET + "\n";
    }
    result += bottomBorder;
    return result;
}

// ==================== 主渲染 ====================
std::string renderMarkdown(const std::string& markdown) {
    std::istringstream stream(markdown);
    std::string line, output;
    enum State { NORMAL, CODE_BLOCK, TABLE } state = NORMAL;
    std::vector<std::string> tableBuffer, codeBuffer;
    std::string codeLang;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // 代码块内
        if (state == CODE_BLOCK) {
            if (line.find("```") == 0) {
                output += formatCodeBlock(codeBuffer, codeLang);
                codeBuffer.clear();
                state = NORMAL;
            } else {
                codeBuffer.push_back(line);
            }
            continue;
        }
        // 表格内
        if (state == TABLE) {
            if (line.find('|') != std::string::npos) {
                tableBuffer.push_back(line);
                continue;
            } else {
                output += formatTable(tableBuffer);
                tableBuffer.clear();
                state = NORMAL;
                // 不要跳过当前行，继续处理
            }
        }
        if (state == NORMAL && line.find('|') != std::string::npos) {
            tableBuffer.push_back(line);
            state = TABLE;
            continue;
        }
        if (line.find("```") == 0) {
            codeLang = (line.size() > 3) ? line.substr(3) : "";
            state = CODE_BLOCK;
            continue;
        }

        // 标题
        if (line.find("### ") == 0) {
            output += std::string(BOLD) + MAGENTA + renderInlines(line) + RESET + "\n";
            continue;
        }
        if (line.find("## ") == 0) {
            output += std::string(BOLD) + BLUE + renderInlines(line) + RESET + "\n";
            continue;
        }
        if (line.find("# ") == 0) {
            output += std::string(BOLD) + CYAN + renderInlines(line) + RESET + "\n";
            continue;
        }
        // 无序列表
        if (std::regex_match(line, std::regex(R"(^\s*[-*+]\s.*)"))) {
            output += std::string(GRAY) + renderInlines(line) + RESET + "\n";
            continue;
        }
        output += renderInlines(line) + "\n";
    }
    // 未闭合的结构
    if (state == CODE_BLOCK && !codeBuffer.empty())
        output += formatCodeBlock(codeBuffer, codeLang);
    if (state == TABLE && !tableBuffer.empty())
        output += formatTable(tableBuffer);

    while (!output.empty() && output.back() == '\n')
        output.pop_back();
    return output;
}