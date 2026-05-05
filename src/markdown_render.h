// src/markdown_render.h
#pragma once
#include <string>

// 将 Markdown 文本渲染为带有 ANSI 转义序列的字符串，直接在终端输出
std::string renderMarkdown(const std::string& markdown);