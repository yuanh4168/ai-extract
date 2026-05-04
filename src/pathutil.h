#pragma once
#include <string>

bool directoryExists(const std::string& path);
bool fileExists(const std::string& path);
void makeDirectory(const std::string& path);
std::string fullPath(const std::string& relative);
bool isPathSafe(const std::string& baseDirAbs, const std::string& relPath, std::string& outAbsolutePath);
std::string getDirectoryTree(const std::string& root);