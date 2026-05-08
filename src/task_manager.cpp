// task_manager.cpp
#include "task_manager.h"
#include "pathutil.h"
#include "utils.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <windows.h>

// ============ 简易 JSON 解析 ============
static std::string jsonString(const std::string& s) {
    size_t start = s.find('"');
    if (start == std::string::npos) return "";
    size_t end = s.find('"', start + 1);
    if (end == std::string::npos) return "";
    return s.substr(start + 1, end - start - 1);
}

static std::vector<std::string> jsonArray(const std::string& s) {
    std::vector<std::string> res;
    size_t pos = s.find('[');
    if (pos == std::string::npos) return res;
    size_t end = s.find(']', pos);
    if (end == std::string::npos) return res;
    std::string arr = s.substr(pos + 1, end - pos - 1);
    size_t p = 0;
    while (p < arr.size()) {
        size_t q = arr.find('"', p);
        if (q == std::string::npos) break;
        size_t e = arr.find('"', q + 1);
        if (e == std::string::npos) break;
        res.push_back(arr.substr(q + 1, e - q - 1));
        p = e + 1;
    }
    return res;
}

std::vector<Task> loadTasks(const std::string& tasksPath, std::string& activeTaskId) {
    std::vector<Task> tasks;
    std::ifstream in(tasksPath);
    if (!in) return tasks;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    size_t atPos = content.find("\"active_task_id\"");
    if (atPos != std::string::npos) {
        size_t colon = content.find(':', atPos);
        if (colon != std::string::npos) {
            std::string rest = content.substr(colon + 1);
            activeTaskId = jsonString(rest);
        }
    }

    size_t taskStart = 0;
    while ((taskStart = content.find('{', taskStart)) != std::string::npos) {
        size_t taskEnd = content.find('}', taskStart);
        if (taskEnd == std::string::npos) break;
        std::string obj = content.substr(taskStart, taskEnd - taskStart + 1);
        taskStart = taskEnd + 1;

        if (obj.find("\"id\"") == std::string::npos) continue;
        Task t;
        auto getVal = [&](const std::string& key) -> std::string {
            size_t pos = obj.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            size_t colon = obj.find(':', pos);
            if (colon == std::string::npos) return "";
            return jsonString(obj.substr(colon + 1));
        };
        t.id = getVal("id");
        t.type = getVal("type");
        t.title = getVal("title");
        t.status = getVal("status");
        t.priority = getVal("priority");
        t.created = getVal("created");
        t.notes = getVal("notes");
        size_t subPos = obj.find("\"subtasks\"");
        if (subPos != std::string::npos) {
            size_t colon = obj.find(':', subPos);
            if (colon != std::string::npos) t.subtasks = jsonArray(obj.substr(colon + 1));
        }
        size_t depPos = obj.find("\"depends_on\"");
        if (depPos != std::string::npos) {
            size_t colon = obj.find(':', depPos);
            if (colon != std::string::npos) t.depends_on = jsonArray(obj.substr(colon + 1));
        }
        tasks.push_back(t);
    }
    return tasks;
}

bool saveTasks(const std::string& tasksPath, const std::vector<Task>& tasks, const std::string& activeTaskId) {
    std::ofstream out(tasksPath);
    if (!out) return false;
    out << "{\n  \"version\": \"1.0\",\n  \"tasks\": [\n";
    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& t = tasks[i];
        out << "    {\n";
        out << "      \"id\": \"" << t.id << "\",\n";
        out << "      \"type\": \"" << t.type << "\",\n";
        out << "      \"title\": \"" << t.title << "\",\n";
        out << "      \"status\": \"" << t.status << "\",\n";
        out << "      \"priority\": \"" << t.priority << "\",\n";
        out << "      \"subtasks\": [";
        for (size_t j = 0; j < t.subtasks.size(); ++j) {
            if (j) out << ", ";
            out << "\"" << t.subtasks[j] << "\"";
        }
        out << "],\n";
        out << "      \"depends_on\": [";
        for (size_t j = 0; j < t.depends_on.size(); ++j) {
            if (j) out << ", ";
            out << "\"" << t.depends_on[j] << "\"";
        }
        out << "],\n";
        out << "      \"created\": \"" << t.created << "\",\n";
        out << "      \"notes\": \"" << t.notes << "\"\n";
        out << "    }";
        if (i != tasks.size() - 1) out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"active_task_id\": " << (activeTaskId.empty() ? "null" : ("\"" + activeTaskId + "\"")) << "\n";
    out << "}\n";
    return true;
}

// ============ STATE_BLOCK 提取（修复 Windows \r\n） ============
std::optional<StateBlock> extractStateBlock(const std::string& text) {
    // 使用 [\r\n]+ 匹配一个或多个换行符，兼容 Windows 和 Unix
    std::regex re(R"(\s*###\s*STATE_BLOCK\s*[\r\n]+([\s\S]*?)###\s*END_STATE)", std::regex::ECMAScript);
    std::smatch match;
    if (!std::regex_search(text, match, re)) return std::nullopt;
    std::string block = match[1].str();
    StateBlock sb;
    auto extractLine = [&](const std::string& key) -> std::string {
        std::regex lineRe(key + R"(\s*:\s*(.*))");
        std::smatch m;
        if (std::regex_search(block, m, lineRe)) return m[1].str();
        return "";
    };
    try { sb.step = std::stoi(extractLine("STEP")); } catch (...) { return std::nullopt; }
    sb.currentSubtask = extractLine("CURRENT_SUBTASK");
    sb.outcome = extractLine("OUTCOME");
    std::string factsLine = extractLine("FACTS");
    if (!factsLine.empty()) {
        std::stringstream ss(factsLine);
        std::string fact;
        while (std::getline(ss, fact, '|')) {
            fact = trim(fact);
            if (!fact.empty()) sb.facts.push_back(fact);
        }
    }
    sb.next = extractLine("NEXT");
    return sb;
}

std::string generateInitialActiveContext(const Task& task) {
    std::ostringstream oss;
    oss << "# 活动上下文\n\n";
    oss << "## 当前任务\n";
    oss << "**任务ID:** " << task.id << "\n";
    oss << "**标题:** " << task.title << "\n";
    oss << "**类型:** " << task.type << "\n";
    oss << "**优先级:** " << task.priority << "\n\n";
    oss << "## 子任务进度\n";
    for (size_t i = 0; i < task.subtasks.size(); ++i)
        oss << "- [ ] " << (i + 1) << ". " << task.subtasks[i] << "\n";
    oss << "\n## 状态块 (由 AI 每轮更新)\n";
    oss << "### STATE_BLOCK\n";
    oss << "STEP: 0\n";
    oss << "CURRENT_SUBTASK: 等待开始\n";
    oss << "OUTCOME: \n";
    oss << "FACTS: \n";
    oss << "NEXT: 开始步骤1：" << (task.subtasks.empty() ? "无子任务" : task.subtasks[0]) << "\n";
    oss << "### END_STATE\n\n";
    oss << "## 补充上下文\n";
    oss << "（本轮对话中需要特别记住的额外信息，如报错日志摘要、临时修改的文件列表等。AI 自行追加。）\n";
    return oss.str();
}

std::string applyStateBlock(const std::string& oldContext, const StateBlock& block) {
    std::string ctx = oldContext;
    // 同样使用 [\r\n] 匹配换行
    std::regex re(R"(\s*###\s*STATE_BLOCK\s*[\r\n]+[\s\S]*?###\s*END_STATE)", std::regex::ECMAScript);
    std::ostringstream newBlock;
    newBlock << "### STATE_BLOCK\n";
    newBlock << "STEP: " << block.step << "\n";
    newBlock << "CURRENT_SUBTASK: " << block.currentSubtask << "\n";
    newBlock << "OUTCOME: " << block.outcome << "\n";
    newBlock << "FACTS: ";
    for (size_t i = 0; i < block.facts.size(); ++i) {
        if (i) newBlock << " | ";
        newBlock << block.facts[i];
    }
    newBlock << "\n";
    newBlock << "NEXT: " << block.next << "\n";
    newBlock << "### END_STATE";
    ctx = std::regex_replace(ctx, re, newBlock.str());
    return ctx;
}

void appendToGlobalMemory(const std::vector<std::string>& facts) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash);
    std::string memoryPath = exeDir + "\\memory_log.md";

    if (!fileExists(memoryPath)) {
        std::ofstream out(memoryPath);
        out << "# 记忆日志\n\n";
    }
    std::ofstream log(memoryPath, std::ios::app);
    if (!log) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &tm);
    log << "\n## " << date << "\n";
    for (const auto& f : facts)
        log << "- " << f << "\n";
}

void backupActiveContext(const std::string& contextPath, const std::string& backupDir) {
    if (!directoryExists(backupDir)) makeDirectory(backupDir);
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    std::ostringstream name;
    name << backupDir << "\\active_context_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".md";
    std::ifstream src(contextPath, std::ios::binary);
    std::ofstream dst(name.str(), std::ios::binary);
    if (src && dst) dst << src.rdbuf();
    
    std::string searchPath = backupDir + "\\active_context_*.md";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    std::vector<std::string> backups;
    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            backups.push_back(backupDir + "\\" + ffd.cFileName);
    } while (FindNextFileA(hFind, &ffd) != 0);
    FindClose(hFind);
    std::sort(backups.begin(), backups.end());
    while (backups.size() > 20) {
        DeleteFileA(backups.front().c_str());
        backups.erase(backups.begin());
    }
}

bool initProjectContext(const std::string& projectRoot, const std::string& exeDir) {
    std::string aiDir = projectRoot + "\\.ai-extract";
    makeDirectory(aiDir);
    
    std::string tasksPath = aiDir + "\\tasks.json";
    if (!fileExists(tasksPath)) {
        std::ofstream out(tasksPath);
        out << "{\n  \"version\": \"1.0\",\n  \"tasks\": [],\n  \"active_task_id\": null\n}\n";
    }
    
    std::string pcPath = aiDir + "\\project_context.md";
    if (!fileExists(pcPath)) {
        std::ofstream out(pcPath);
        out << "# 项目永久上下文\n\n"
            << "## 技术栈\n- 语言: (待填写)\n- 框架: (待填写)\n\n"
            << "## 项目结构\n- (待填写)\n\n"
            << "## 编码规范\n- (待填写)\n\n"
            << "## 历史经验\n- (待填写)\n";
    }
    makeDirectory(aiDir + "\\context_backups");
    return true;
}