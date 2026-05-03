# ai-extract

AI 多文件代码自动提取工具

从剪贴板（或文件）读取 AI 输出的代码片段，自动解析出多个文件并保存到指定目录，同时支持 Git 和 Zip 备份。

## 功能

- 📋 自动读取系统剪贴板中的 `###FILE: ` 标记
- 📁 支持多文件批量创建，自动处理路径
- ⚠️ 启发式空函数/空类警告（C/C++/Python/JS 等）
- 💾 文件保存前可预览、确认
- 🔒 路径安全检查（拒绝 `..` 和绝对路径）
- 🔄 循环模式：可连续处理多次剪贴板内容
- 🗃️ 自动 Git 初始化和提交
- 📦 自动 Zip 压缩备份
- 🌐 中英双语提示

## 编译

### Windows (MinGW)

    g++ -std=c++17 -o ai-extract.exe ai-extract.cpp -luser32

### Linux

    g++ -std=c++17 -o ai-extract ai-extract.cpp -lstdc++fs

### macOS

    clang++ -std=c++17 -o ai-extract ai-extract.cpp

## 使用方法

    ai-extract -o my_output_dir --debug

详细参数请用 `-h` 查看。

## 剪贴板输入格式

    ###FILE: src/main.cpp
    #include <iostream>
    int main() { std::cout << "Hello"; return 0; }

## 项目结构

    ai-extract/
    ├── ai-extract.cpp    # 源代码
    ├── ai-extract.exe    # 编译产物
    ├── README.md         # 本说明
    └── .gitignore

