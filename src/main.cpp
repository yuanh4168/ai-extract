// src/main.cpp
#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <direct.h>
#include "config.h"
#include "utils.h"
#include "clipboard.h"
#include "parser.h"
#include "directiveproc.h"
#include "promptchain.h"
#include "pathutil.h"
#include "markdown_render.h"
#include "task_manager.h"
#include "web_browser.h"
#include "easy_mode.h"

static void printUsage() {
    CLR_INFO << "用法: ai-extract [选项]\n"
              << "  -o <dir>       输出目录\n"
              << "  -f             强制覆盖\n"
              << "  -i <file>      从文本文件读取\n"
              << "  --no-backup    跳过备份\n"
              << "  --debug        调试模式\n"
              << "  --auto         自动模式\n"
              << "  --loop         循环模式\n"
              << "  -h, --help     显示命令行帮助\n"
              << "  --task <cmd>   任务管理 (init / start <id> / stop / status)\n";
}

static std::string getExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string s(path);
    size_t pos = s.find_last_of("\\/");
    return (pos == std::string::npos) ? "." : s.substr(0, pos);
}

static void handleTaskAfterProcessing(const std::string& clipboardText, Config& cfg) {
    std::string workDir = fullPath(cfg.outDir);
    std::string tasksPath = workDir + "\\.ai-extract\\tasks.json";
    std::string activeCtxPath = workDir + "\\.ai-extract\\active_context.md";
    std::string backupDir = workDir + "\\.ai-extract\\context_backups";

    if (!fileExists(tasksPath)) return;
    std::string activeTaskId;
    auto tasks = loadTasks(tasksPath, activeTaskId);
    if (activeTaskId.empty()) return;

    auto it = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == activeTaskId; });
    if (it == tasks.end()) return;

    auto stateBlock = extractStateBlock(clipboardText);
    if (!stateBlock) {
        CLR_WARN << "[任务] 未在 AI 回复中找到有效的 STATE_BLOCK，请检查格式。\n";
        return;
    }

    CLR_SUCCESS << "[任务] 解析 STATE_BLOCK: STEP " << stateBlock->step 
                << " | 子任务: " << stateBlock->currentSubtask 
                << " | NEXT: " << stateBlock->next << "\n";

    std::string oldCtx;
    if (fileExists(activeCtxPath)) {
        std::ifstream in(activeCtxPath);
        std::ostringstream ss; ss << in.rdbuf();
        oldCtx = ss.str();
    }
    std::string newCtx = oldCtx.empty() ? "" : applyStateBlock(oldCtx, *stateBlock);
    if (!newCtx.empty()) {
        std::ofstream out(activeCtxPath);
        out << newCtx;
    }

    static int lastBackupStep = 0;
    if (stateBlock->step % 5 == 0 && stateBlock->step != lastBackupStep) {
        backupActiveContext(activeCtxPath, backupDir);
        lastBackupStep = stateBlock->step;
        CLR_INFO << "[任务] 已自动备份 active_context.md\n";
    }

    if (!stateBlock->facts.empty())
        appendToGlobalMemory(stateBlock->facts);

    if (stateBlock->next == "TASK_COMPLETE" || stateBlock->next.empty()) {
        it->status = "done";
        activeTaskId = "";
        CLR_SUCCESS << "[任务] 任务 " << it->id << " 已完成！\n";
    } else if (stateBlock->next.find("ABANDON") != std::string::npos) {
        it->status = "abandoned";
        activeTaskId = "";
        CLR_WARN << "[任务] 任务 " << it->id << " 已放弃。\n";
    } else {
        auto& subs = it->subtasks;
        if (std::find(subs.begin(), subs.end(), stateBlock->next) == subs.end()) {
            subs.push_back(stateBlock->next);
            CLR_INFO << "[任务] AI 建议新增子任务: " << stateBlock->next << "\n";
        }
    }

    saveTasks(tasksPath, tasks, activeTaskId);
}

// 新增：生成下一轮对话的上下文（合并 project_context.md 和 active_context.md）
static std::string generateNextContext(const Config& cfg) {
    std::string pcPath = fullPath(cfg.outDir) + "\\.ai-extract\\project_context.md";
    std::string activeCtxPath = fullPath(cfg.outDir) + "\\.ai-extract\\active_context.md";
    std::ostringstream combined;
    if (fileExists(pcPath)) {
        std::ifstream in(pcPath);
        combined << in.rdbuf() << "\n\n";
    }
    if (fileExists(activeCtxPath)) {
        std::ifstream in(activeCtxPath);
        combined << in.rdbuf();
    }
    return combined.str();
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }

    std::string configPath = "ai-extract.ini";
    Config cfg;
    bool configLoaded = cfg.load(configPath);

    if (configLoaded && !cfg.startupDir.empty()) {
        if (setCurrentDir(cfg.startupDir)) CLR_INFO << "初始目录: " << getCurrentDir() << "\n";
    }

    bool cmdForce = false, cmdNoBackup = false, cmdDebug = false, cmdAuto = false, cmdLoop = false;
    std::string cmdOutDir, inputFile, taskCmd;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) cmdOutDir = argv[++i];
        else if (arg == "-f") cmdForce = true;
        else if (arg == "--no-backup") cmdNoBackup = true;
        else if (arg == "--debug") cmdDebug = true;
        else if (arg == "--auto") cmdAuto = true;
        else if (arg == "--loop") cmdLoop = true;
        else if (arg == "-i" && i + 1 < argc) inputFile = argv[++i];
        else if (arg == "-h" || arg == "--help") { printUsage(); return 0; }
        else if (arg == "--task" && i + 1 < argc) {
            taskCmd = argv[++i];
            while (i + 1 < argc && argv[i+1][0] != '-') {
                taskCmd += " " + std::string(argv[++i]);
            }
        }
        else { CLR_ERROR << "未知选项: " << arg << "\n"; return 1; }
    }

    if (!cmdOutDir.empty()) cfg.outDir = cmdOutDir;
    if (cfg.outDir.empty()) cfg.outDir = ".";
    if (cmdForce) cfg.force = true;
    if (cmdNoBackup) cfg.noBackup = true;
    if (cmdDebug) cfg.debug = true;

    if (!taskCmd.empty()) {
        std::string workDir = fullPath(cfg.outDir);
        std::string aiDir = workDir + "\\.ai-extract";
        std::string tasksPath = aiDir + "\\tasks.json";
        std::string activeCtxPath = aiDir + "\\active_context.md";
        std::string pcPath = aiDir + "\\project_context.md";
        std::string exeDir = getExeDir();

        initProjectContext(workDir, exeDir);

        std::istringstream iss(taskCmd);
        std::string cmd, arg;
        iss >> cmd >> arg;
        if (cmd == "init") {
            CLR_SUCCESS << "[任务] 项目记忆目录已初始化: " << aiDir << "\n";
            return 0;
        }
        else if (cmd == "start" && !arg.empty()) {
            std::string activeTaskId;
            auto tasks = loadTasks(tasksPath, activeTaskId);
            auto it = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == arg; });
            if (it == tasks.end()) {
                CLR_ERROR << "[任务] 任务 ID " << arg << " 不存在。\n";
                return 1;
            }
            std::string st = trim(it->status);
            if (st == "done" || st == "abandoned") {
                CLR_ERROR << "[任务] 任务 " << it->id << " 已完成或已放弃，无法再次启动。\n";
                return 1;
            }
            if (!activeTaskId.empty()) {
                CLR_ERROR << "[任务] 已有活动任务 " << activeTaskId << "，请先暂停或等待完成。\n";
                return 1;
            }
            if (!it->depends_on.empty()) {
                for (auto& dep : it->depends_on) {
                    auto depIt = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == dep && t.status == "done"; });
                    if (depIt == tasks.end()) {
                        CLR_ERROR << "[任务] 依赖任务 " << dep << " 未完成，无法启动。\n";
                        return 1;
                    }
                }
            }
            it->status = "in_progress";
            activeTaskId = it->id;
            saveTasks(tasksPath, tasks, activeTaskId);
            std::string ctx = generateInitialActiveContext(*it);
            std::ofstream out(activeCtxPath);
            out << ctx;
            std::ostringstream prompt;
            if (fileExists(pcPath)) {
                std::ifstream in(pcPath);
                prompt << in.rdbuf() << "\n\n";
            }
            prompt << ctx;
            writeClipboard(prompt.str());
            CLR_SUCCESS << "[任务] 已启动任务 " << it->id << "，上下文已复制到剪贴板。\n";
            return 0;
        }
        else if (cmd == "stop") {
            std::string activeTaskId;
            auto tasks = loadTasks(tasksPath, activeTaskId);
            if (activeTaskId.empty()) {
                CLR_WARN << "[任务] 当前没有活动任务。\n";
                return 0;
            }
            auto it = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == activeTaskId; });
            if (it != tasks.end()) it->status = "blocked";
            activeTaskId = "";
            saveTasks(tasksPath, tasks, activeTaskId);
            CLR_SUCCESS << "[任务] 已暂停。\n";
            return 0;
        }
        else if (cmd == "status") {
            std::string activeTaskId;
            auto tasks = loadTasks(tasksPath, activeTaskId);
            CLR_INFO << "活动任务: " << (activeTaskId.empty() ? "无" : activeTaskId) << "\n";
            for (auto& t : tasks)
                CLR_INFO << "  [" << t.status << "] " << t.id << " " << t.title << "\n";
            return 0;
        }
        else {
            CLR_ERROR << "未知任务命令: " << cmd << "\n";
            return 1;
        }
    }

    if (!inputFile.empty()) {
        std::ifstream in(inputFile);
        if (!in) { CLR_ERROR << "无法打开文件: " << inputFile << "\n"; return 1; }
        std::ostringstream ss; ss << in.rdbuf();
        auto dirs = parseDirectives(ss.str());
        std::string outputAccum = processDirectives(dirs, cfg);
        // 文件输入模式没有任务上下文，只输出执行结果
        writeClipboard(outputAccum);
        cfg.save(configPath);
        return 0;
    }

    std::string mode;
    if (cmdAuto) mode = "auto";
    else if (cmdLoop) mode = "loop";
    else if (configLoaded) mode = cfg.defaultMode;
    else mode = "interactive";

    if (mode == "auto") {
        std::string text = readClipboard();
        if (text.empty()) {
            CLR_WARN << "剪贴板为空\n";
        }
        else if (text.size() > cfg.maxClipboardSize) {
            CLR_WARN << "剪贴板内容过大，已跳过。\n";
            writeLog(LogLevel::WARN, "Auto mode: clipboard too large");
        }
        else {
            auto dirs = parseDirectives(text);
            std::string outputAccum = processDirectives(dirs, cfg);
            handleTaskAfterProcessing(text, cfg);
            // 统一缓冲区：合并任务上下文
            std::string ctx = generateNextContext(cfg);
            if (!ctx.empty()) {
                if (!outputAccum.empty()) outputAccum += "\n---\n";
                outputAccum += ctx;
            }
            writeClipboard(outputAccum);
        }
    } else {
        bool quit = false;
        while (!quit) {
            CLR_INPUT << "\n按回车读取剪贴板，或输入命令 (:help) > ";
            std::string userIn;
            std::getline(std::cin, userIn);
            userIn = trim(userIn);
            if (userIn.empty()) {
                std::string text = readClipboard();
                if (text.empty()) {
                    CLR_WARN << "剪贴板为空\n";
                    continue;
                }
                if (text.size() > cfg.maxClipboardSize) {
                    CLR_WARN << "剪贴板内容过大，已跳过。\n";
                    writeLog(LogLevel::WARN, "Clipboard too large, skipped.");
                    continue;
                }
                if (cfg.debug) CLR_INFO << "剪贴板内容:\n" << text << "\n";
                auto dirs = parseDirectives(text);
                std::string outputAccum = processDirectives(dirs, cfg);
                handleTaskAfterProcessing(text, cfg);
                // 统一缓冲区：合并任务上下文
                std::string ctx = generateNextContext(cfg);
                if (!ctx.empty()) {
                    if (!outputAccum.empty()) outputAccum += "\n---\n";
                    outputAccum += ctx;
                }
                writeClipboard(outputAccum);
                continue;
            }
            if (userIn[0] == ':') {
                std::istringstream iss(userIn);
                std::string cmd; iss >> cmd;
                cmd = cmd.substr(1);
                std::string arg; std::getline(iss, arg);
                arg = trim(arg);

                if (cmd == "help") {
                    char exePath[MAX_PATH];
                    GetModuleFileNameA(NULL, exePath, MAX_PATH);
                    std::string exeDir(exePath);
                    size_t lastSlash = exeDir.find_last_of("\\/");
                    if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash);
                    std::string readmePath = exeDir + "\\README.md";
                    bool plain = (arg == "--plain");
                    CLR_INFO << "正在显示: " << readmePath << "\n";
                    if (!fileExists(readmePath)) {
                        CLR_WARN << "README.md 未找到: " << readmePath << "\n";
                    } else {
                        std::ifstream in(readmePath, std::ios::binary);
                        if (!in) {
                            CLR_ERROR << "无法打开文件: " << readmePath << "\n";
                        } else {
                            std::ostringstream oss;
                            oss << in.rdbuf();
                            std::string content = oss.str();
                            if (content.empty()) {
                                CLR_WARN << "文件内容为空。\n";
                            } else {
                                if (plain) std::cout << content << std::endl;
                                else std::cout << renderMarkdown(content) << std::endl;
                            }
                            std::cout << "\n按回车键继续...";
                            std::cin.clear();
                            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            std::cin.get();
                        }
                    }
                }
                else if (cmd == "dir") {
                    if (arg.empty()) CLR_INFO << "当前目录: " << getCurrentDir() << "\n";
                    else {
                        if (setCurrentDir(arg)) { CLR_SUCCESS << "工作目录切换到: " << arg << "\n"; cfg.startupDir = arg; }
                        else CLR_ERROR << "无法切换\n";
                    }
                }
                else if (cmd == "out") {
                    if (!arg.empty()) { cfg.outDir = arg; CLR_SUCCESS << "输出目录设置为: " << fullPath(arg) << "\n"; }
                    else CLR_WARN << "当前输出目录: " << fullPath(cfg.outDir) << "\n";
                }
                else if (cmd == "force") {
                    if (arg == "on") cfg.force = true;
                    else if (arg == "off") cfg.force = false;
                    else cfg.force = !cfg.force;
                    CLR_SUCCESS << "强制覆盖: " << (cfg.force ? "开" : "关") << "\n";
                }
                else if (cmd == "debug") {
                    if (arg == "on") cfg.debug = true;
                    else if (arg == "off") cfg.debug = false;
                    else cfg.debug = !cfg.debug;
                    CLR_SUCCESS << "调试模式: " << (cfg.debug ? "开" : "关") << "\n";
                }
                else if (cmd == "backup") {
                    if (arg == "on") cfg.noBackup = false;
                    else if (arg == "off") cfg.noBackup = true;
                    else cfg.noBackup = !cfg.noBackup;
                    CLR_SUCCESS << "备份: " << (cfg.noBackup ? "关" : "开") << "\n";
                }
                else if (cmd == "auto") {
                    std::string text = readClipboard();
                    if (!text.empty()) {
                        if (text.size() > cfg.maxClipboardSize) { CLR_WARN << "剪贴板内容过大，已跳过。\n"; }
                        else {
                            auto dirs = parseDirectives(text);
                            std::string outputAccum = processDirectives(dirs, cfg);
                            handleTaskAfterProcessing(text, cfg);
                            std::string ctx = generateNextContext(cfg);
                            if (!ctx.empty()) {
                                if (!outputAccum.empty()) outputAccum += "\n---\n";
                                outputAccum += ctx;
                            }
                            writeClipboard(outputAccum);
                        }
                    }
                }
                else if (cmd == "prompt") { promptChainMode(cfg); }
                else if (cmd == "tree") {
                    std::string tree = getDirectoryTree(fullPath(cfg.outDir));
                    if (tree.empty()) CLR_INFO << "输出目录为空\n";
                    else { CLR_INFO << tree; writeClipboard(tree); }
                }
                else if (cmd == "open") {
                    std::string path = fullPath(cfg.outDir);
                    ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    CLR_SUCCESS << "已打开文件夹: " << path << "\n";
                }
                else if (cmd == "readme") {
                    std::string readmePath = "README.md";
                    bool plain = false;
                    if (!arg.empty()) {
                        if (arg == "--plain") plain = true;
                        else readmePath = arg;
                    }
                    std::string absReadme = fullPath(readmePath);
                    CLR_INFO << "正在读取: " << absReadme << "\n";
                    if (!fileExists(absReadme)) { CLR_WARN << "文件不存在: " << absReadme << "\n"; }
                    else {
                        std::ifstream in(absReadme, std::ios::binary);
                        if (!in) CLR_ERROR << "无法打开文件: " << absReadme << "\n";
                        else {
                            std::ostringstream oss;
                            oss << in.rdbuf();
                            std::string content = oss.str();
                            CLR_INFO << "文件大小: " << content.size() << " 字节\n";
                            if (content.empty()) CLR_WARN << "文件内容为空。\n";
                            else {
                                if (plain) std::cout << content << std::endl;
                                else std::cout << renderMarkdown(content) << std::endl;
                            }
                            std::cout << "\n按回车键继续...";
                            std::cin.clear();
                            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            std::cin.get();
                        }
                    }
                }
                else if (cmd == "quit") { quit = true; }
                else if (cmd == "task") {
                    std::string workDir = fullPath(cfg.outDir);
                    std::string aiDir = workDir + "\\.ai-extract";
                    std::string tasksPath = aiDir + "\\tasks.json";
                    std::string activeCtxPath = aiDir + "\\active_context.md";
                    std::string pcPath = aiDir + "\\project_context.md";
                    std::string exeDir = getExeDir();
                    initProjectContext(workDir, exeDir);

                    std::istringstream iss(arg);
                    std::string act, taskId;
                    iss >> act >> taskId;
                    if (act == "init") {
                        CLR_SUCCESS << "[任务] 项目记忆目录已初始化。\n";
                    }
                    else if (act == "start" && !taskId.empty()) {
                        std::string activeTaskId;
                        auto tasks = loadTasks(tasksPath, activeTaskId);
                        auto it = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == taskId; });
                        if (it == tasks.end()) {
                            CLR_ERROR << "[任务] 任务 ID 不存在。\n";
                        } else {
                            std::string st = trim(it->status);
                            if (st == "done" || st == "abandoned") {
                                CLR_ERROR << "[任务] 任务 " << it->id << " 已完成或已放弃，无法再次启动。\n";
                            } else if (!activeTaskId.empty()) {
                                CLR_ERROR << "[任务] 已有活动任务 " << activeTaskId << "，请先暂停或等待完成。\n";
                            } else {
                                if (!it->depends_on.empty()) {
                                    bool depsMet = true;
                                    for (auto& dep : it->depends_on) {
                                        auto depIt = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == dep && t.status == "done"; });
                                        if (depIt == tasks.end()) { depsMet = false; break; }
                                    }
                                    if (!depsMet) {
                                        CLR_ERROR << "[任务] 依赖任务未完成，无法启动。\n";
                                        continue;
                                    }
                                }
                                it->status = "in_progress";
                                activeTaskId = it->id;
                                saveTasks(tasksPath, tasks, activeTaskId);
                                std::string ctx = generateInitialActiveContext(*it);
                                std::ofstream out(activeCtxPath);
                                out << ctx;
                                std::ostringstream prompt;
                                if (fileExists(pcPath)) {
                                    std::ifstream in(pcPath);
                                    prompt << in.rdbuf() << "\n\n";
                                }
                                prompt << ctx;
                                writeClipboard(prompt.str());
                                CLR_SUCCESS << "[任务] 已启动，上下文已复制到剪贴板。\n";
                            }
                        }
                    }
                    else if (act == "stop") {
                        std::string activeTaskId;
                        auto tasks = loadTasks(tasksPath, activeTaskId);
                        if (activeTaskId.empty()) {
                            CLR_WARN << "[任务] 当前没有活动任务。\n";
                        } else {
                            auto it = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == activeTaskId; });
                            if (it != tasks.end()) it->status = "blocked";
                            activeTaskId = "";
                            saveTasks(tasksPath, tasks, activeTaskId);
                            CLR_SUCCESS << "[任务] 已暂停。\n";
                        }
                    }
                    else if (act == "status") {
                        std::string activeTaskId;
                        auto tasks = loadTasks(tasksPath, activeTaskId);
                        CLR_INFO << "活动任务: " << (activeTaskId.empty() ? "无" : activeTaskId) << "\n";
                        for (auto& t : tasks)
                            CLR_INFO << "  [" << t.status << "] " << t.id << " " << t.title << "\n";
                    }
                    else {
                        CLR_WARN << "用法: :task init|start <id>|stop|status\n";
                    }
                }
                else if (cmd == "browse") {
                    if (arg.empty()) {
                        CLR_WARN << "用法: :browse <url>\n";
                    } else {
                        std::string result = browsePage(arg, cfg.maxReadSize);
                        if (result.find("Error:") == 0) {
                            CLR_ERROR << result << "\n";
                        } else {
                            writeClipboard(result);
                            CLR_SUCCESS << "[BROWSE] 内容已复制到剪贴板\n";
                        }
                    }
                }
                else if (cmd == "easy") {
                    easyModeMenu(cfg);
                }
                // 新增 :next 命令
                else if (cmd == "next") {
                    std::string ctx = generateNextContext(cfg);
                    if (ctx.empty()) {
                        CLR_WARN << "[next] 没有可用的上下文。\n";
                    } else {
                        writeClipboard(ctx);
                        CLR_SUCCESS << "[next] 已将下一轮对话上下文复制到剪贴板。\n";
                    }
                }
                else { CLR_WARN << "未知命令: " << cmd << "\n"; }
            }
        }
    }

    cfg.save(configPath);
    CLR_INFO << "程序结束，按回车键退出...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}