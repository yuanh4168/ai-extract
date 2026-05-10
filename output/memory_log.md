# 记忆日志



## 2026-05-10 14:22
- NEXT: 实现主窗口创建和消息循环

## 2026-05-10 14:27
- 使用Win32 API CreateWindowEx创建按钮
- 使用DrawText绘制数字，需自定义字体
- 窗口过程通过WM_COMMAND响应按钮
- 全局时间字符串g_timeText用于刷新显示

## 2026-05-10 14:30
- 使用SetTimer和WM_TIMER实现每秒tick
- CountdownTimer类封装状态和剩余秒数
- WM_CREATE中初始化Timer，WM_DESTROY中清理
- 倒计时至0时自动停止并弹出提示
- 暂停时KillTimer停止刷新，开始时SetTimer重新启动
