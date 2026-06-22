# Windows 命令行解释器

这是一个使用 C++17 和 Win32 API 实现的简易 Windows 命令行解释器，满足任务书中对 `cd`、`dir`、`history`、`exit`、`tasklist`、`taskkill` 等内部命令的要求。程序采用控制台交互方式运行，提示符显示当前工作目录和 `>`。

## 目录结构

```text
code/
├─ CMakeLists.txt
├─ README.md
├─ docs/
│  └─ Windows命令行解释器设计文档.md
├─ include/
│  ├─ CommandParser.h
│  ├─ Shell.h
│  └─ WinUtil.h
└─ src/
   ├─ CommandParser.cpp
   ├─ Shell.cpp
   ├─ WinUtil.cpp
   └─ main.cpp
```

## 编译方法

直接使用 g++

```powershell
g++ -std=c++17 -Wall -Wextra -Iinclude src\main.cpp src\CommandParser.cpp src\Shell.cpp src\WinUtil.cpp -o winshell.exe
```

## 支持命令

| 命令 | 功能 |
| --- | --- |
| `cd [path]` | 切换或显示当前目录 |
| `dir [path|wildcard]` | 显示目录内容、文件大小、目录数、磁盘剩余空间 |
| `history [clear]` | 显示或清空历史命令 |
| `exit [code]` | 退出解释器 |
| `tasklist` | 显示系统当前进程信息 |
| `taskkill <pid>` | 按 PID 结束进程 |
| `taskkill /PID <pid>` | 按 PID 结束进程 |
| `echo [text]` | 输出文本，支持 `echo %ERRORLEVEL%` |
| `pwd` | 显示当前目录 |
| `cls` | 清屏 |
| `help` | 显示帮助 |

不是内部命令的输入会通过 `CreateProcessW` 创建外部进程执行；如果直接创建失败，程序会再尝试通过系统 `ComSpec` 即 `cmd.exe /C` 执行，以兼容部分批处理和系统命令。

