#include "promptchain.h"
#include "utils.h"
#include "clipboard.h"
#include "pathutil.h"
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
        if (content.find("{{REQUIREMENTS}}") != std::string::npos)
            return content;
        else
            return content + "\n{{REQUIREMENTS}}"; // 如果没有标记，添加到末尾
    }
    // 回退到硬编码的默认模板
    return R"(
你是一个严格遵循输出格式的代码生成与文件操作助手。你只能输出两类内容：回答用户问题和输出代码，每一次回答的正文部分只能包含上面的这两类别的其中一个，回答用户问题正常的回答，给出代码的部分必须且只能使用下述四种指令来响应，不能添加任何解释、说明、问候或 Markdown 装饰。整个回复由指令序列构成，指令之间由空行分隔。

## 指令语法

### 1. 创建/更新文件
###FILE: 相对路径/文件名
文件完整内容（可多行）

text

- 路径使用正斜杠 `/`，相对于项目根目录。
- 路径不得包含 `..` 或以 `/` 开头（绝对路径），否则会被拒绝。
- 文件内容原样保留，包括缩进和空行。如果内容中包含三反引号，直接书写即可，工具会自动处理。

### 2. 读取文件内容到剪贴板
###READ: 相对路径/文件名

text

- 工具会将对应文件的内容（或路径，取决于配置）复制到剪贴板。

### 3. 删除文件
###DELETE: 相对路径/文件名

text

- 工具会要求用户两次确认后才执行删除。

### 4. 执行命令行命令
###EXEC:
命令行指令

text

### 5. 浏览网页
###BROWSE: URL

text

## 格式约束
- 整个回复中不能出现任何自然语言文本。

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
            } else if (cmd == "generate") {
                std::string combinedReqs;
                for (auto& req : requirements) combinedReqs += req + "\n";
                std::string prompt = promptTemplate;
                size_t pos = prompt.find("{{REQUIREMENTS}}");
                if (pos != std::string::npos)
                    prompt.replace(pos, std::string("{{REQUIREMENTS}}").length(), combinedReqs);
                else
                    prompt += "\n" + combinedReqs; // 保护措施
                std::string tree = getDirectoryTree(fullPath(cfg.outDir));
                if (!tree.empty())
                    prompt += "\n当前项目的文件结构如下（供参考）：\n" + tree;
                writeClipboard(prompt);
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
                break;
            } else CLR_WARN << "未知命令: " << cmd << "\n";
        } else {
            requirements.push_back(input);
            CLR_SUCCESS << "已添加需求 (" << requirements.size() << "): " << input << "\n";
        }
    }
}