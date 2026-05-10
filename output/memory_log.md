# 记忆日志


## 2026-05-10 14:58
- Win32中SetTimer的ID需非零，否则KillTimer可能失败
- 通过GWLP_USERDATA传递对象指针是封装窗口过程的高效方法
- ES_NUMBER风格仅允许输入数字，是限制编辑框输入的简便方式

## 2026-05-10 14:59
- 使用 cl.exe 编译 Win32 GUI 程序需要链接 user32.lib，命令行参数 /EHsc 启用 C++ 异常处理
- 编译时需注意头文件依赖关系，确保所有自定义头文件在同一目录或通过 /I 指定

## 2026-05-10 15:01
- MinGW 编译 Windows GUI 应用需加 -mwindows 选项以避免控制台窗口，若缺少可能导致程序无窗口或附带控制台
- 链接 Win32 API 至少需要 -luser32，基本控件无需 comctl32
- 中文乱码在命令行中常见，可能是终端编码非 UTF-8，使用 MinGW 时可通过 chcp 65001 临时解决

## 2026-05-10 15:02
- MinGW 中 swprintf_s 不是标准函数，使用 wsprintfW (Windows API) 可以安全格式化宽字符串且无需额外头文件
- 在 MinGW 下编译 Win32 GUI 应用必须使用 -mwindows 选项以避免控制台窗口并正确链接入口点
- 文件尾部意外混入非代码文本会导致编译错误

## 2026-05-10 15:03
- MinGW-w64 链接 wWinMain 需要 -municode 选项，否则默认寻找 WinMain 导致未定义引用
- 为保证 Unicode API 一致性，应始终在包含 windows.h 前定义 UNICODE 和 _UNICODE 宏
- 编译选项 -mwindows 和 -municode 可以组合使用以生成纯 Windows GUI 应用并匹配 Unicode 入口点

## 2026-05-10 15:16
- MinGW 编译 Windows GUI 应用需加 -mwindows 选项以避免控制台窗口，若缺少可能导致程序无窗口或附带控制台
- 链接 Win32 API 至少需要 -luser32，基本控件无需 comctl32
- 中文乱码在命令行中常见，可能是终端编码非 UTF-8，使用 MinGW 时可通过 chcp 65001 临时解决
