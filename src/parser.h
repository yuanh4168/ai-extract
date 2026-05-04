#pragma once
#include "common.h"

std::vector<FileDirective> parseDirectives(const std::string& text);
std::vector<Warning> detectEmptyBodies(const std::vector<std::pair<std::string, std::string>>& files);