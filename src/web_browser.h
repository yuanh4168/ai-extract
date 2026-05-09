#pragma once
#include <string>
#include <cstddef>

/**
 * 抓取指定 URL 的纯文本摘要
 * @param url 必须以 http:// 或 https:// 开头
 * @param maxChars 最大返回字符数
 * @return 纯文本内容，若失败返回错误信息字符串
 */
std::string browsePage(const std::string& url, size_t maxChars = 6000);