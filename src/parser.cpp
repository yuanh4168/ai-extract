#include "parser.h"
#include "utils.h"
#include <regex>

std::vector<FileDirective> parseDirectives(const std::string& text) {
    std::vector<FileDirective> directives;
    std::regex re(R"(^###(FILE|READ|DELETE|EXEC):\s*([^\r\n]*))");
    auto lines = splitLines(text);
    std::string currentType, currentPath;
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
                    for (size_t i = 0; i < clines.size(); ++i) {
                        if (i > 0) cleaned += '\n';
                        cleaned += clines[i];
                    }
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
            currentContent.str("");
            currentContent.clear();
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
            for (size_t i = 0; i < clines.size(); ++i) {
                if (i > 0) cleaned += '\n';
                cleaned += clines[i];
            }
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

static void detectCStyle(const std::string& file, const std::vector<std::string>& lines, std::vector<Warning>& warnings) {
    std::regex emptyBodyRe(R"(\)\s*\{\s*\})");
    std::regex returnBodyRe(R"(\)\s*\{\s*return\s*;\s*\})");
    for (int i = 0; i < (int)lines.size(); ++i) {
        const std::string& line = lines[i];
        if (std::regex_search(line, emptyBodyRe))
            warnings.push_back({file, i + 1, trim(line) + " (empty body)"});
        else if (std::regex_search(line, returnBodyRe))
            warnings.push_back({file, i + 1, trim(line) + " (empty body, return;)"});
        else if (std::regex_search(line, std::regex(R"(\)\s*\{)")) && line.find('}') == std::string::npos) {
            int j = i + 1;
            while (j < (int)lines.size() && lines[j].find_first_not_of(" \t\r\n") == std::string::npos) ++j;
            if (j < (int)lines.size() && trim(lines[j]) == "}")
                warnings.push_back({file, i + 1, trim(line) + " ... } (empty body)"});
        }
    }
}

static void detectPython(const std::string& file, const std::vector<std::string>& lines, std::vector<Warning>& warnings) {
    for (int i = 0; i < (int)lines.size(); ++i) {
        const std::string& line = lines[i];
        std::string t = trim(line);
        if ((t.rfind("def ", 0) == 0 || t.rfind("async def ", 0) == 0) && t.back() == ':') {
            size_t indent = line.find_first_not_of(" \t");
            if (indent == std::string::npos) indent = 0;
            int j = i + 1;
            while (j < (int)lines.size() && lines[j].find_first_not_of(" \t\r\n") == std::string::npos) ++j;
            if (j < (int)lines.size()) {
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

std::vector<Warning> detectEmptyBodies(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<Warning> warnings;
    for (const auto& [path, content] : files) {
        auto lines = splitLines(content);
        std::string ext;
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) ext = path.substr(dot);
        if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext == ".java" || ext == ".cs")
            detectCStyle(path, lines, warnings);
        else if (ext == ".py")
            detectPython(path, lines, warnings);
        else if (ext == ".js" || ext == ".ts" || ext == ".jsx" || ext == ".tsx" || ext == ".mjs")
            detectCStyle(path, lines, warnings);
    }
    return warnings;
}