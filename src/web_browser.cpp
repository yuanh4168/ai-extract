#include "web_browser.h"
#include <cstdio>
#include <regex>
#include <sstream>
#include <memory>
#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

static bool curlAvailable() {
    std::unique_ptr<FILE, decltype(&PCLOSE)> pipe(POPEN("curl --version 2>nul", "r"), PCLOSE);
    if (!pipe) return false;
    char buf[128];
    if (fgets(buf, sizeof(buf), pipe.get()) != nullptr)
        return std::string(buf).find("curl") != std::string::npos;
    return false;
}

static std::string fetchHTML(const std::string& url, int timeoutSec = 15) {
    // 修改点：添加 2>&1 合并 stderr，添加 Accept-Language 头
    std::string cmd = "curl -sL --max-time " + std::to_string(timeoutSec) +
                      " -A \"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\""
                      " -H \"Accept-Language: zh-CN,zh;q=0.9\" \"" +
                      url + "\" 2>&1";
    std::unique_ptr<FILE, decltype(&PCLOSE)> pipe(POPEN(cmd.c_str(), "r"), PCLOSE);
    if (!pipe) return "";
    std::ostringstream out;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr)
        out << buffer;
    std::string result = out.str();
    // 检查 curl 自身的错误信息
    if (result.size() >= 6 && result.substr(0, 6) == "curl: ") {
        size_t end = result.find_last_not_of("\r\n");
        if (end != std::string::npos) result = result.substr(0, end + 1);
        return "";  // 返回空字符串，后续由 browsePage 给出明确错误
    }
    return result;
}

static void removeTagContent(std::string& html, const std::string& tag) {
    std::regex open(R"(<\s*)" + tag + R"([^>]*>)", std::regex::icase);
    std::regex close(R"(<\s*/\s*)" + tag + R"(\s*>)", std::regex::icase);
    size_t pos = 0;
    while (pos < html.size()) {
        std::smatch mOpen;
        auto itOpen = html.cbegin() + pos;
        if (!std::regex_search(itOpen, html.cend(), mOpen, open)) break;
        size_t start = pos + mOpen.position();
        size_t afterOpen = start + mOpen.length();
        std::smatch mClose;
        auto itClose = html.cbegin() + afterOpen;
        if (std::regex_search(itClose, html.cend(), mClose, close)) {
            size_t endClose = afterOpen + mClose.position() + mClose.length();
            html.erase(start, endClose - start);
            pos = start;
        } else {
            html.erase(start);
            break;
        }
    }
}

static std::string extractBody(const std::string& html) {
    std::regex bodyBegin(R"(<\s*body[^>]*>)", std::regex::icase);
    std::regex bodyEnd(R"(<\s*/\s*body\s*>)", std::regex::icase);
    std::smatch mStart, mEnd;
    if (std::regex_search(html, mStart, bodyBegin)) {
        auto startPos = mStart.position() + mStart.length();
        auto searchStart = html.cbegin() + startPos;
        if (std::regex_search(searchStart, html.cend(), mEnd, bodyEnd)) {
            auto endPos = startPos + mEnd.position();
            return html.substr(startPos, endPos - startPos);
        }
    }
    return html;
}

static std::string stripHTMLTags(const std::string& input) {
    std::string temp = input;

    temp = std::regex_replace(temp,
        std::regex(R"~(<\s*a\s[^>]*href\s*=\s*"([^"]*)"[^>]*>([\s\S]*?)<\s*/\s*a\s*>)~", std::regex::icase),
        "$2 [$1]");

    temp = std::regex_replace(temp, std::regex(R"(<\s*h1[^>]*>)", std::regex::icase), "\n## ");
    temp = std::regex_replace(temp, std::regex(R"(<\s*h2[^>]*>)", std::regex::icase), "\n### ");
    temp = std::regex_replace(temp, std::regex(R"(<\s*h3[^>]*>)", std::regex::icase), "\n#### ");
    temp = std::regex_replace(temp, std::regex(R"(<\s*/\s*h[1-6]\s*>)", std::regex::icase), "\n");
    temp = std::regex_replace(temp, std::regex(R"(<\s*br\s*/?\s*>)", std::regex::icase), "\n");

    temp = std::regex_replace(temp, std::regex(R"(<[^>]*>)"), " ");

    temp = std::regex_replace(temp, std::regex("&nbsp;"), " ");
    temp = std::regex_replace(temp, std::regex("&amp;"), "&");
    temp = std::regex_replace(temp, std::regex("&lt;"), "<");
    temp = std::regex_replace(temp, std::regex("&gt;"), ">");

    temp = std::regex_replace(temp, std::regex("\n{3,}"), "\n\n");
    temp = std::regex_replace(temp, std::regex(" {2,}"), " ");

    return temp;
}

std::string browsePage(const std::string& url, size_t maxChars) {
    if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) {
        return "Error: Only http/https URLs are allowed.";
    }

    if (!curlAvailable()) {
        return "Error: curl not found. Please install curl and add it to PATH.";
    }

    std::string html = fetchHTML(url);
    if (html.empty()) {
        // 修改点：提供详细错误原因
        return "Error: 无法获取网页内容。可能原因：\n"
               "  1. 网站屏蔽了自动化访问（返回403）\n"
               "  2. 网络连接超时或DNS解析失败\n"
               "  3. SSL证书验证失败\n"
               "  4. URL 无效或不存在\n"
               "URL: " + url;
    }

    removeTagContent(html, "script");
    removeTagContent(html, "style");
    removeTagContent(html, "nav");
    removeTagContent(html, "footer");
    removeTagContent(html, "header");

    std::string body = extractBody(html);
    std::string text = stripHTMLTags(body);

    if (text.size() > maxChars) {
        text = text.substr(0, maxChars) + "\n\n... [Content truncated]";
    }

    return text;
}