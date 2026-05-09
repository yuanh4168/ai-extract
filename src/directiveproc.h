#pragma once
#include "common.h"
#include "config.h"
#include <string>

// 返回所有累积的输出文本（浏览、EXEC 等），不再内部写剪贴板
std::string processDirectives(const std::vector<FileDirective>& directives, const Config& cfg);