#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include "config.h"
#include "utils.h"
#include "clipboard.h"
#include "parser.h"
#include "directiveproc.h"
#include "promptchain.h"
#include "pathutil.h"

static void printHelp() {
    CLR_INFO << "用法: ai-extract [选项]\n"
              << "  -o <dir>       输出目录\n"
              << "  -f             强制覆盖\n"
              << "  -i <file>      从文本文件读取\n"
              << "  --no-backup    跳过备份\n"
              << "  --debug        调试模式\n"
              << "  --auto         自动模式\n"
              << "  --loop         循环模式\n"
              << "  -h, --help     帮助\n";
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::string configPath = "ai-extract.ini";
    Config cfg;
    bool configLoaded = cfg.load(configPath);

    if (configLoaded && !cfg.startupDir.empty()) {
        if (setCurrentDir(cfg.startupDir)) CLR_INFO << "初始目录: " << getCurrentDir() << "\n";
    }

    bool cmdForce = false, cmdNoBackup = false, cmdDebug = false, cmdAuto = false, cmdLoop = false;
    std::string cmdOutDir, inputFile;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i+1 < argc) cmdOutDir = argv[++i];
        else if (arg == "-f") cmdForce = true;
        else if (arg == "--no-backup") cmdNoBackup = true;
        else if (arg == "--debug") cmdDebug = true;
        else if (arg == "--auto") cmdAuto = true;
        else if (arg == "--loop") cmdLoop = true;
        else if (arg == "-i" && i+1 < argc) inputFile = argv[++i];
        else if (arg == "-h" || arg == "--help") { printHelp(); return 0; }
        else { CLR_ERROR << "未知选项: " << arg << "\n"; return 1; }
    }

    if (!cmdOutDir.empty()) cfg.outDir = cmdOutDir;
    if (cfg.outDir.empty()) cfg.outDir = ".";
    if (cmdForce) cfg.force = true;
    if (cmdNoBackup) cfg.noBackup = true;
    if (cmdDebug) cfg.debug = true;

    if (!inputFile.empty()) {
        std::ifstream in(inputFile);
        if (!in) { CLR_ERROR << "无法打开文件: " << inputFile << "\n"; return 1; }
        std::ostringstream ss; ss << in.rdbuf();
        auto dirs = parseDirectives(ss.str());
        processDirectives(dirs, cfg);
        cfg.save(configPath);
        return 0;
    }

    std::string mode;
    if (cmdAuto) mode = "auto";
    else if (cmdLoop) mode = "loop";
    else if (configLoaded) mode = cfg.defaultMode;
    else mode = "interactive";

    if (mode == "auto") {
        std::string text;
        try { text = readClipboard(); } catch (...) { CLR_ERROR << "读取剪贴板失败\n"; }
        if (text.empty()) { CLR_WARN << "剪贴板为空\n"; }
        else if (text.size() > cfg.maxClipboardSize) {
            CLR_WARN << "剪贴板内容过大 (" << text.size() << " 字节)，超出限制 (" << cfg.maxClipboardSize << ")，已跳过。\n";
            writeLog(LogLevel::WARN, "Auto mode: clipboard size exceeded limit");
        } else {
            auto dirs = parseDirectives(text);
            processDirectives(dirs, cfg);
        }
    } else {
        bool quit = false;
        while (!quit) {
            CLR_INPUT << "\n按回车读取剪贴板，或输入命令 (:help) > ";
            std::string userIn;
            std::getline(std::cin, userIn);
            userIn = trim(userIn);
            if (userIn.empty()) {
                std::string text;
                try { text = readClipboard(); } catch (const std::exception& e) { CLR_ERROR << "读取失败: " << e.what() << "\n"; continue; }
                if (text.empty()) { CLR_WARN << "剪贴板为空\n"; continue; }
                if (text.size() > cfg.maxClipboardSize) {
                    CLR_WARN << "剪贴板内容过大 (" << text.size() << " 字节)，超出限制 (" << cfg.maxClipboardSize << ")，已跳过。\n";
                    writeLog(LogLevel::WARN, "Clipboard too large, skipped.");
                    continue;
                }
                if (cfg.debug) CLR_INFO << "剪贴板内容:\n" << text << "\n";
                auto dirs = parseDirectives(text);
                processDirectives(dirs, cfg);
                continue;
            }
            if (userIn[0] == ':') {
                std::istringstream iss(userIn);
                std::string cmd; iss >> cmd; cmd = cmd.substr(1);
                std::string arg; std::getline(iss, arg); arg = trim(arg);
                if (cmd == "help") {
                    CLR_INFO << "命令: :help :dir :out :force :debug :backup :auto :prompt :tree :open :quit\n";
                } else if (cmd == "dir") {
                    if (arg.empty()) CLR_INFO << "当前目录: " << getCurrentDir() << "\n";
                    else {
                        if (setCurrentDir(arg)) {
                            CLR_SUCCESS << "工作目录切换到: " << arg << "\n";
                            cfg.startupDir = arg;
                        } else CLR_ERROR << "无法切换\n";
                    }
                } else if (cmd == "out") {
                    if (!arg.empty()) { cfg.outDir = arg; CLR_SUCCESS << "输出目录设置为: " << fullPath(arg) << "\n"; }
                    else CLR_WARN << "当前输出目录: " << fullPath(cfg.outDir) << "\n";
                } else if (cmd == "force") {
                    if (arg == "on") cfg.force = true;
                    else if (arg == "off") cfg.force = false;
                    else cfg.force = !cfg.force;
                    CLR_SUCCESS << "强制覆盖: " << (cfg.force ? "开" : "关") << "\n";
                } else if (cmd == "debug") {
                    if (arg == "on") cfg.debug = true;
                    else if (arg == "off") cfg.debug = false;
                    else cfg.debug = !cfg.debug;
                    CLR_SUCCESS << "调试模式: " << (cfg.debug ? "开" : "关") << "\n";
                } else if (cmd == "backup") {
                    if (arg == "on") cfg.noBackup = false;
                    else if (arg == "off") cfg.noBackup = true;
                    else cfg.noBackup = !cfg.noBackup;
                    CLR_SUCCESS << "备份: " << (cfg.noBackup ? "关" : "开") << "\n";
                } else if (cmd == "auto") {
                    std::string text;
                    try { text = readClipboard(); } catch (...) { CLR_ERROR << "读取失败\n"; }
                    if (!text.empty()) {
                        if (text.size() > cfg.maxClipboardSize) {
                            CLR_WARN << "剪贴板内容过大，已跳过。\n";
                            writeLog(LogLevel::WARN, "Clipboard too large during auto command");
                        } else {
                            auto dirs = parseDirectives(text);
                            processDirectives(dirs, cfg);
                        }
                    }
                } else if (cmd == "prompt") {
                    promptChainMode(cfg);
                } else if (cmd == "tree") {
                    std::string tree = getDirectoryTree(fullPath(cfg.outDir));
                    if (tree.empty()) CLR_INFO << "输出目录为空\n";
                    else { CLR_INFO << tree; writeClipboard(tree); }
                } else if (cmd == "open") {
                    std::string path = fullPath(cfg.outDir);
                    ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    CLR_SUCCESS << "已打开文件夹: " << path << "\n";
                } else if (cmd == "quit") quit = true;
            }
        }
    }

    cfg.save(configPath);
    CLR_INFO << "程序结束，按回车键退出...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}