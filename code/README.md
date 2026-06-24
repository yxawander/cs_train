# Windows 命令行解释器

这是一个使用 C++17 和 Win32 API 实现的 Windows 命令行解释器，满足任务书中对 `cd`、`dir`、`history`、`exit`、`tasklist`、`taskkill` 等内部命令的要求，并额外扩展了文件管理、环境变量、历史命令查看和进程过滤等功能。程序采用控制台交互方式运行，提示符显示当前工作目录和 `>`。

## 目录结构

```text
code/
├─ README.md
├─ 设计文档.md
├─ winshell.exe
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
| `cd [path]` / `chdir [path]` | 切换或显示当前目录 |
| `dir [path\|wildcard]` | 显示目录内容、文件大小、目录数、磁盘剩余空间 |
| `mkdir <dir>` / `md <dir>` | 创建目录 |
| `rmdir <dir>` / `rd <dir>` | 删除空目录 |
| `del <file\|wildcard>` / `erase <file\|wildcard>` | 删除文件，支持通配符 |
| `copy <src> <dst>` | 复制文件 |
| `move <src> <dst>` / `ren <src> <dst>` / `rename <src> <dst>` | 移动或重命名文件、目录 |
| `type <file>` | 显示文本文件内容 |
| `history [n\|clear]` | 显示历史命令、最近 n 条命令或清空历史 |
| `exit [code]` | 退出解释器 |
| `tasklist [keyword]` | 显示系统当前进程信息，可按进程名过滤 |
| `taskkill <pid>` | 按 PID 结束进程 |
| `taskkill /PID <pid> [/F]` | 按 PID 结束进程 |
| `taskkill /IM <name> [/F]` | 按进程名结束进程 |
| `set [name[=value]]` | 显示、查询、设置或删除环境变量 |
| `echo [text]` | 输出文本，支持 `%ERRORLEVEL%`、`%CD%` 和 `%变量名%` |
| `pwd` | 显示当前目录 |
| `date` / `time` | 显示当前日期或时间 |
| `ver` | 显示程序版本、用户名和计算机名 |
| `cls` / `clear` | 使用控制台 API 清屏 |
| `help [command]` / `? [command]` | 显示总体帮助或指定命令帮助 |

不是内部命令的输入会通过 `CreateProcessW` 创建外部进程执行；如果直接创建失败，程序会再尝试通过系统 `ComSpec` 即 `cmd.exe /C` 执行，以兼容部分批处理和系统命令。

## 示例

```text
mkdir test
copy README.md test\README-copy.md
type test\README-copy.md
move test\README-copy.md test\README-moved.md
del test\README-moved.md
rmdir test

set DEMO=hello
echo %DEMO%
history 5

tasklist chrome
taskkill /IM notepad.exe /F
```
