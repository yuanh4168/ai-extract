// task_manager.h
#pragma once
#include <string>
#include <vector>
#include <optional>
#include "config.h"

struct Task {
    std::string id;
    std::string type;       // "bug" or "feature"
    std::string title;
    std::string status;     // new, in_progress, blocked, done, abandoned
    std::string priority;   // high, medium, low
    std::vector<std::string> subtasks;
    std::vector<std::string> depends_on;
    std::string created;
    std::string notes;
};

struct StateBlock {
    int step = 0;
    std::string currentSubtask;
    std::string outcome;
    std::vector<std::string> facts;   // 拆分后的多条事实
    std::string next;
};

// 解析 tasks.json，返回任务列表和当前活动任务 ID
std::vector<Task> loadTasks(const std::string& tasksPath, std::string& activeTaskId);
// 保存 tasks.json
bool saveTasks(const std::string& tasksPath, const std::vector<Task>& tasks, const std::string& activeTaskId);

// 从文本中提取 STATE_BLOCK，返回 nullopt 表示未找到或格式错误
std::optional<StateBlock> extractStateBlock(const std::string& text);

// 生成初始 active_context.md 内容
std::string generateInitialActiveContext(const Task& task);

// 根据 StateBlock 更新 active_context.md，返回新内容
std::string applyStateBlock(const std::string& oldContext, const StateBlock& block);

// 将事实追加到全局 memory_log.md（exe 同目录）
void appendToGlobalMemory(const std::vector<std::string>& facts);

// 备份 active_context.md（每5轮调用），保留最近20份
void backupActiveContext(const std::string& contextPath, const std::string& backupDir);

// 初始化目标项目的 .ai-extract 结构（创建目录和默认文件）
bool initProjectContext(const std::string& projectRoot, const std::string& exeDir);