#include "promptchain.h"
#include "utils.h"
#include "clipboard.h"
#include "pathutil.h"
#include "markdown_render.h"
#include <fstream>
#include <sstream>
#include <windows.h>

static std::string loadPromptTemplate(const std::string& exeDir) {
    std::string templatePath = exeDir + "\\prompt_template.md";
    std::ifstream in(templatePath);
    if (in) {
        std::ostringstream oss;
        oss << in.rdbuf();
        std::string content = oss.str();
        if (content.find("{{REQUIREMENTS}}") == std::string::npos) {
            content += "\n{{REQUIREMENTS}}";
        }
        return content;
    }
    // 内置回退
    return R"(
## 设定
你是一个严格遵循输出格式的代码生成与文件操作助手...
## 当前需求：
{{REQUIREMENTS}}
)";
}

void promptChainMode(Config& cfg) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash);

    std::string promptTemplate = loadPromptTemplate(exeDir);
    std::vector<std::string> requirements;

    CLR_INFO << "=== 提示词链模式 ===\n";
    CLR_INFO << "当前使用的提示词模板:\n" << renderMarkdown(promptTemplate) << "\n";
    CLR_INFO << "请输入你的第一条需求，或输入 :help 查看命令\n";

    while (true) {
        CLR_INPUT << "需求> ";
        std::string input;
        std::getline(std::cin, input);
        input = trim(input);
        if (input.empty()) continue;

        if (input[0] == ':') {
            std::string cmd = input.substr(1);
            if (cmd == "help") {
                CLR_INFO << "命令: :help :history :undo :reset :generate :tree :quit/:exit\n";
            } else if (cmd == "history") {
                if (requirements.empty()) CLR_INFO << "暂无需求\n";
                else for (size_t i = 0; i < requirements.size(); ++i) CLR_INFO << "  " << i+1 << ". " << requirements[i] << "\n";
            } else if (cmd == "undo") {
                if (requirements.empty()) CLR_WARN << "没有可撤销的需求\n";
                else { requirements.pop_back(); CLR_SUCCESS << "已撤销最后一条需求\n"; }
            } else if (cmd == "reset") {
                requirements.clear(); CLR_SUCCESS << "需求已清空\n";
            } else if (cmd == "generate" || cmd == "done") {
                std::string combinedReqs;
                for (auto& req : requirements) combinedReqs += req + "\n";
                std::string prompt = promptTemplate;
                size_t pos = prompt.find("{{REQUIREMENTS}}");
                if (pos != std::string::npos)
                    prompt.replace(pos, std::string("{{REQUIREMENTS}}").length(), combinedReqs);
                else
                    prompt += "\n" + combinedReqs;
                std::string tree = getDirectoryTree(fullPath(cfg.outDir));
                if (!tree.empty())
                    prompt += "\n当前项目的文件结构如下（供参考）：\n" + tree;
                writeClipboard(prompt);
                CLR_SUCCESS << "提示词已生成并复制到剪贴板。\n";
                return;   // 生成后直接退出
            } else if (cmd == "tree") {
                std::string tree = getDirectoryTree(fullPath(cfg.outDir));
                if (tree.empty()) {
                    CLR_INFO << "输出目录为空或不存在。\n";
                } else {
                    CLR_INFO << "当前项目文件结构:\n" << tree;
                    std::string treeEntry = "当前项目的文件结构（参考）：\n" + tree;
                    requirements.push_back(treeEntry);
                    CLR_SUCCESS << "已将项目结构添加为需求 (" << requirements.size() << ")\n";
                }
            } else if (cmd == "quit" || cmd == "exit") {
                CLR_INFO << "退出提示词模式\n";
                return;
            } else {
                CLR_WARN << "未知命令: " << cmd << "\n";
            }
        } else {
            requirements.push_back(input);
            CLR_SUCCESS << "已添加需求 (" << requirements.size() << "): " << input << "\n";
        }
    }
}