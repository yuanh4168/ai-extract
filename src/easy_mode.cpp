#include "easy_mode.h"
#include "utils.h"
#include "pathutil.h"
#include "clipboard.h"
#include "parser.h"
#include "directiveproc.h"
#include "promptchain.h"
#include "web_browser.h"
#include "task_manager.h"
#include <windows.h>
#include <shellapi.h>
#include <conio.h>
#include <vector>
#include <string>
#include <functional>
#include <sstream>
#include <fstream>

struct MenuNode {
    std::string label;
    std::vector<MenuNode> children;
    std::function<void(Config&)> action;
    bool isAction = false;
};

// 获取可执行文件所在目录
static std::string getAiExtractExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string s(path);
    size_t pos = s.find_last_of("\\/");
    return (pos == std::string::npos) ? "." : s.substr(0, pos);
}

static void drawMenu(const std::vector<MenuNode>& nodes, int selected) {
    system("cls");
    CLR_INFO << "=== 简易模式 === (↑↓移动 回车/空格选择 Esc返回 q退出)\n\n";
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (static_cast<int>(i) == selected)
            std::cout << " > ";
        else
            std::cout << "   ";
        CLR_INPUT << nodes[i].label << "\n";
    }
    std::cout << std::flush;
}

// 通用列表选择器，返回被选中的字符串，若取消返回空串
static std::string selectFromList(const std::vector<std::string>& items, const std::string& title) {
    if (items.empty()) return "";
    int selected = 0;
    while (true) {
        system("cls");
        CLR_INFO << title << "\n\n";
        for (size_t i = 0; i < items.size(); ++i) {
            if (static_cast<int>(i) == selected) std::cout << " > ";
            else std::cout << "   ";
            CLR_INPUT << items[i] << "\n";
        }
        int ch = _getch();
        if (ch == 0 || ch == 224) {
            int key = _getch();
            if (key == 72 && selected > 0) selected--;
            else if (key == 80 && selected < (int)items.size()-1) selected++;
        } else if (ch == 13 || ch == 32) {
            return items[selected];
        } else if (ch == 27) {
            return "";
        }
    }
}

static bool navigate(Config& cfg, std::vector<MenuNode>& currentLevel) {
    int selected = 0;
    drawMenu(currentLevel, selected);
    while (true) {
        int ch = _getch();
        if (ch == 0 || ch == 224) {
            int key = _getch();
            if (key == 72) {
                if (selected > 0) selected--;
                drawMenu(currentLevel, selected);
            } else if (key == 80) {
                if (selected < (int)currentLevel.size() - 1) selected++;
                drawMenu(currentLevel, selected);
            }
        } else if (ch == 13 || ch == 32) {
            MenuNode& node = currentLevel[selected];
            if (node.isAction) {
                if (node.action) node.action(cfg);
            } else if (!node.children.empty()) {
                if (!navigate(cfg, node.children)) {
                    return false;
                }
                drawMenu(currentLevel, selected);
            }
        } else if (ch == 27) {
            return true;
        } else if (ch == 'q' || ch == 'Q') {
            return false;
        }
    }
}

void easyModeMenu(Config& cfg) {
    // 构建根菜单
    std::vector<MenuNode> root = {
        {
            "创建文件",
            {
                {
                    "输入文件名和内容",
                    {},
                    [&](Config& c) {
                        CLR_INPUT << "请输入文件名: ";
                        std::string fname;
                        std::getline(std::cin, fname);
                        CLR_INPUT << "请输入文件内容 (输入 END 结束):\n";
                        std::string content, line;
                        while (std::getline(std::cin, line) && line != "END")
                            content += line + "\n";
                        std::string full = "###FILE: " + fname + "\n" + content;
                        auto dirs = parseDirectives(full);
                        processDirectives(dirs, c);
                        CLR_INPUT << "\n按任意键继续...";
                        _getch();
                    },
                    true
                },
                {"返回", {}, [](Config&){}, true}
            },
            nullptr,
            false
        },
        {
            "读取文件到剪贴板",
            {},
            [&](Config& c) {
                CLR_INPUT << "请输入文件名: ";
                std::string fname;
                std::getline(std::cin, fname);
                std::string full = "###READ: " + fname;
                auto dirs = parseDirectives(full);
                processDirectives(dirs, c);
                CLR_INPUT << "\n按任意键继续...";
                _getch();
            },
            true
        },
        {
            "删除文件",
            {},
            [&](Config& c) {
                CLR_INPUT << "请输入文件名: ";
                std::string fname;
                std::getline(std::cin, fname);
                std::string full = "###DELETE: " + fname;
                auto dirs = parseDirectives(full);
                processDirectives(dirs, c);
                CLR_INPUT << "\n按任意键继续...";
                _getch();
            },
            true
        },
        {
            "浏览网页",
            {},
            [&](Config& c) {
                CLR_INPUT << "请输入 URL: ";
                std::string url;
                std::getline(std::cin, url);
                std::string result = browsePage(url, c.maxReadSize);
                if (result.find("Error:") == 0)
                    CLR_ERROR << result << "\n";
                else {
                    writeClipboard(result);
                    CLR_SUCCESS << "内容已复制到剪贴板\n";
                }
                CLR_INPUT << "\n按任意键继续...";
                _getch();
            },
            true
        },
        {
            "执行命令",
            {},
            [&](Config& c) {
                CLR_INPUT << "请输入命令: ";
                std::string cmd;
                std::getline(std::cin, cmd);
                std::string full = "###EXEC:\n" + cmd;
                auto dirs = parseDirectives(full);
                processDirectives(dirs, c);
                CLR_INPUT << "\n按任意键继续...";
                _getch();
            },
            true
        },
        {
            "查看项目文件树",
            {},
            [&](Config& c) {
                std::string tree = getDirectoryTree(fullPath(c.outDir));
                if (tree.empty()) CLR_INFO << "输出目录为空\n";
                else CLR_INFO << tree;
                CLR_INPUT << "\n按任意键继续...";
                _getch();
            },
            true
        },
        {
            "打开输出目录",
            {},
            [&](Config& c) {
                std::string path = fullPath(c.outDir);
                ShellExecuteA(NULL, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                CLR_SUCCESS << "已打开文件夹\n";
                CLR_INPUT << "\n按任意键继续...";
                _getch();
            },
            true
        },
        // 新增：启动新项目（提示词链）
        {
            "启动新项目（生成提示词）",
            {},
            [&](Config& c) {
                promptChainMode(c);
            },
            true
        },
        // 新增：项目任务维护
        {
            "项目任务维护",
            {
                {
                    "开始任务",
                    {},
                    [&](Config& c) {
                        std::string workDir = fullPath(c.outDir);
                        std::string aiDir = workDir + "\\.ai-extract";
                        std::string tasksPath = aiDir + "\\tasks.json";
                        std::string activeCtxPath = aiDir + "\\active_context.md";
                        std::string pcPath = aiDir + "\\project_context.md";
                        initProjectContext(workDir, getAiExtractExeDir());

                        std::string activeTaskId;
                        auto tasks = loadTasks(tasksPath, activeTaskId);
                        if (tasks.empty()) {
                            CLR_WARN << "没有可用的任务。\n按任意键继续...";
                            _getch();
                            return;
                        }
                        std::vector<std::string> taskItems;
                        for (auto& t : tasks) {
                            taskItems.push_back(t.id + " [" + t.status + "] " + t.title);
                        }
                        std::string sel = selectFromList(taskItems, "选择要启动的任务 (Esc取消)");
                        if (sel.empty()) return;
                        std::string taskId = sel.substr(0, sel.find(' '));
                        auto it = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == taskId; });
                        if (it == tasks.end()) return;
                        std::string st = trim(it->status);
                        if (st == "done" || st == "abandoned") {
                            CLR_ERROR << "任务 " << it->id << " 已完成或已放弃。\n按任意键继续...";
                            _getch();
                            return;
                        }
                        if (!activeTaskId.empty()) {
                            CLR_ERROR << "已有活动任务 " << activeTaskId << "，请先暂停。\n按任意键继续...";
                            _getch();
                            return;
                        }
                        for (auto& dep : it->depends_on) {
                            auto depIt = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == dep && t.status == "done"; });
                            if (depIt == tasks.end()) {
                                CLR_ERROR << "依赖任务 " << dep << " 未完成。\n按任意键继续...";
                                _getch();
                                return;
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
                        CLR_SUCCESS << "任务已启动，上下文已复制到剪贴板。\n按任意键继续...";
                        _getch();
                    },
                    true
                },
                {
                    "暂停活动任务",
                    {},
                    [&](Config& c) {
                        std::string workDir = fullPath(c.outDir);
                        std::string tasksPath = workDir + "\\.ai-extract\\tasks.json";
                        std::string activeTaskId;
                        auto tasks = loadTasks(tasksPath, activeTaskId);
                        if (activeTaskId.empty()) {
                            CLR_WARN << "当前没有活动任务。\n按任意键继续...";
                            _getch();
                            return;
                        }
                        auto it = std::find_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == activeTaskId; });
                        if (it != tasks.end()) it->status = "blocked";
                        activeTaskId = "";
                        saveTasks(tasksPath, tasks, activeTaskId);
                        CLR_SUCCESS << "任务已暂停。\n按任意键继续...";
                        _getch();
                    },
                    true
                },
                {
                    "查看任务状态",
                    {},
                    [&](Config& c) {
                        std::string workDir = fullPath(c.outDir);
                        std::string tasksPath = workDir + "\\.ai-extract\\tasks.json";
                        std::string activeTaskId;
                        auto tasks = loadTasks(tasksPath, activeTaskId);
                        CLR_INFO << "活动任务: " << (activeTaskId.empty() ? "无" : activeTaskId) << "\n";
                        for (auto& t : tasks)
                            CLR_INFO << "  [" << t.status << "] " << t.id << " " << t.title << "\n";
                        CLR_INPUT << "\n按任意键继续...";
                        _getch();
                    },
                    true
                },
                {
                    "继续下一轮对话 (生成 next 提示词)",
                    {},
                    [&](Config& c) {
                        std::string workDir = fullPath(c.outDir);
                        std::string pcPath = workDir + "\\.ai-extract\\project_context.md";
                        std::string activeCtxPath = workDir + "\\.ai-extract\\active_context.md";
                        std::ostringstream combined;
                        if (fileExists(pcPath)) {
                            std::ifstream in(pcPath);
                            combined << in.rdbuf() << "\n\n";
                        }
                        if (fileExists(activeCtxPath)) {
                            std::ifstream in(activeCtxPath);
                            combined << in.rdbuf();
                        }
                        std::string result = combined.str();
                        if (result.empty()) {
                            CLR_WARN << "没有可用的上下文。\n按任意键继续...";
                        } else {
                            writeClipboard(result);
                            CLR_SUCCESS << "下一轮对话上下文已复制到剪贴板。\n按任意键继续...";
                        }
                        _getch();
                    },
                    true
                },
                {"返回", {}, [](Config&){}, true}
            },
            nullptr,
            false
        },
        {
            "退出简易模式",
            {},
            [](Config&){},
            true
        }
    };

    navigate(cfg, root);
    system("cls");
    CLR_INFO << "已退出简易模式\n";
}