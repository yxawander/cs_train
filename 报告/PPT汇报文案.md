Windows 命令行解释器设计与实现 PPT 汇报文案

第 1 页：标题页

标题：
Windows 命令行解释器设计与实现

副标题：
基于 C++17 与 Win32 API 的控制台 Shell

页面内容：
姓名：
班级：
指导教师：
日期：

讲稿：
各位老师好，我本次实训完成的是 Windows 命令行解释器设计与实现。项目使用 C++17 编写，主要通过 Win32 API 实现目录管理、文件管理、进程管理、环境变量管理、控制台操作以及外部命令执行等功能。


第 2 页：任务要求与完成情况

标题：
任务要求与完成情况

页面内容：
任务书要求：

- 设计 Windows 控制台命令解释器
- 提示符显示当前目录和 >
- 读取用户输入命令并执行
- 实现常用内部命令
- 学习并调用 Windows API
- 支持外部命令执行

核心命令完成情况：

| 任务书要求命令 | 实现情况 | 说明 |
| --- | --- | --- |
| cd | 已完成 | 支持显示和切换当前目录，同时支持 chdir |
| dir | 已完成 | 显示目录内容、文件大小、磁盘剩余空间 |
| history | 已完成 | 支持全部历史、最近 n 条、清空历史 |
| exit | 已完成 | 支持退出解释器和指定退出码 |
| tasklist | 已完成 | 显示进程名、PID、线程数、父进程 PID |
| taskkill | 已完成 | 支持按 PID 和按进程名结束进程 |

扩展功能：
文件管理、环境变量、外部命令执行、控制台清屏、帮助命令、系统信息命令。

讲稿：
任务书要求实现一个类似 Windows Command 的命令解释器，核心命令包括 cd、dir、history、exit、tasklist 和 taskkill。目前这些命令都已经完成。在此基础上，我还扩展了文件管理、环境变量、清屏、帮助和系统信息等功能，使程序更接近实际命令行工具。


第 3 页：系统架构与主流程

标题：
系统架构与执行流程

页面内容：
模块划分：

- CommandParser：命令解析，负责命令名、参数、双引号路径处理
- Shell：主循环、提示符、历史记录、内部命令分发、外部命令执行
- WinUtil：Win32 辅助函数，负责路径、错误信息、时间、格式化等

总体流程：

```mermaid
flowchart TD
    A[用户输入命令] --> B[Shell 主循环读取输入]
    B --> C[CommandParser 解析命令]
    C --> D{是否为空命令}
    D -->|是| B
    D -->|否| E[保存到 history_]
    E --> F{是否为内部命令}
    F -->|是| G[调用 cmdXXX 内部命令函数]
    F -->|否| H[CreateProcessW 执行外部命令]
    G --> I[调用 Win32 API]
    H --> I
    I --> B
```

内部命令分发：

- help / ?
- cd / chdir
- dir
- history
- exit
- tasklist
- taskkill
- echo
- pwd
- mkdir / md
- rmdir / rd
- del / erase
- copy
- move / ren / rename
- type
- set
- date / time / ver
- cls / clear

讲稿：
系统分为三个模块。CommandParser 负责解析用户输入，Shell 负责控制主循环和命令分发，WinUtil 封装一些常用 Win32 辅助函数。程序每次读取一行命令，解析后先判断是否为空，再保存到历史记录，然后判断是不是内部命令。如果是内部命令，就调用对应函数；如果不是，就通过 CreateProcessW 执行外部程序。


第 4 页：命令解析与内部/外部命令机制

标题：
命令解析与执行机制

页面内容：
命令解析支持：

- 去除首尾空白
- 命令名统一转小写
- 支持双引号路径
- 保存原始命令行
- 参数列表化

解析示例：

```text
cd "C:\Program Files"
dir "*.cpp"
taskkill /PID 1234
copy "a b.txt" "test dir\a b.txt"
```

内部命令：

- 由 Shell 自己处理
- 直接调用 Windows API
- 例如 cd 必须内部实现，因为当前目录属于 Shell 进程自身状态

外部命令：

- 非内部命令通过 CreateProcessW 执行
- WaitForSingleObject 等待结束
- GetExitCodeProcess 保存退出码
- 如果直接执行失败，则使用 cmd.exe /C 兼容执行

外部命令示例：

```text
ipconfig
where cmd
echo %ERRORLEVEL%
```

讲稿：
命令解析模块支持双引号路径，所以带空格路径也可以正确处理。解析后，程序会先判断是否为内部命令。内部命令由 Shell 自己完成，例如 cd 必须内部实现，因为如果让子进程执行 cd，只会改变子进程目录。外部命令则通过 CreateProcessW 创建进程，并用 WaitForSingleObject 等待结束，最后保存退出码。


第 5 页：核心命令实现：目录与历史

标题：
核心命令实现一：目录与历史

页面内容：
cd / chdir：

- 不带参数时显示当前目录
- 带路径时切换目录
- 支持 cd /d D:\test
- 失败时输出 Windows 错误信息

dir：

- 支持 dir、dir .、dir 路径、dir 通配符
- 输出文件时间、文件大小、目录标记、文件名
- 统计文件数、目录数、总字节数
- 显示卷序列号和磁盘剩余空间

history：

- 每条非空命令保存到 history_
- history 显示全部历史
- history n 显示最近 n 条
- history clear 清空历史

核心 API：

| 命令 | 核心 API / 数据结构 | 作用 |
| --- | --- | --- |
| cd / chdir | GetCurrentDirectoryW, SetCurrentDirectoryW | 获取和切换当前目录 |
| dir | FindFirstFileW, FindNextFileW | 枚举目录内容 |
| dir | GetVolumeInformationW, GetDiskFreeSpaceExW | 获取卷信息和磁盘剩余空间 |
| history | std::vector<std::wstring> | 保存历史命令 |

讲稿：
目录相关命令是任务书的重点。cd 通过 SetCurrentDirectoryW 改变 Shell 自身当前目录。dir 通过 FindFirstFileW 和 FindNextFileW 枚举目录项，同时使用 GetVolumeInformationW 和 GetDiskFreeSpaceExW 显示磁盘信息。history 则使用 vector 保存用户输入过的非空命令。


第 6 页：核心命令实现：进程管理

标题：
核心命令实现二：进程管理

页面内容：
tasklist：

- CreateToolhelp32Snapshot 创建进程快照
- Process32FirstW 读取第一个进程
- Process32NextW 读取后续进程
- 输出进程名、PID、线程数、父进程 PID
- 支持 tasklist keyword 关键字过滤

taskkill：

- 支持 taskkill 1234
- 支持 taskkill /PID 1234
- 支持 taskkill /IM notepad.exe /F
- 检查 PID 是否有效
- 拒绝结束当前 Shell 自身进程
- 权限不足或进程不存在时输出错误信息

taskkill 流程：

```mermaid
flowchart TD
    A[解析 taskkill 参数] --> B{按 PID 还是进程名}
    B -->|PID| C[OpenProcess 打开目标进程]
    B -->|进程名| D[枚举进程查找匹配 PID]
    D --> C
    C --> E[TerminateProcess 终止进程]
    E --> F[WaitForSingleObject 等待退出]
    F --> G[CloseHandle 关闭句柄]
```

核心 API：

| 命令 | 核心 API | 作用 |
| --- | --- | --- |
| tasklist | CreateToolhelp32Snapshot | 创建进程快照 |
| tasklist | Process32FirstW / Process32NextW | 遍历进程 |
| taskkill | OpenProcess | 打开目标进程 |
| taskkill | TerminateProcess | 终止目标进程 |
| taskkill | WaitForSingleObject / CloseHandle | 等待退出并释放句柄 |

讲稿：
tasklist 和 taskkill 体现了进程管理 API 的使用。tasklist 通过进程快照遍历系统进程，输出进程名、PID、线程数和父进程 PID。taskkill 支持按 PID 和进程名结束进程，真正结束时先用 OpenProcess 打开进程，再用 TerminateProcess 终止，最后关闭句柄。


第 7 页：扩展功能：文件、环境变量与控制台

标题：
扩展功能实现

页面内容：
文件管理命令：

| 命令 | 功能 | API |
| --- | --- | --- |
| mkdir / md | 创建目录 | CreateDirectoryW |
| rmdir / rd | 删除空目录 | RemoveDirectoryW |
| del / erase | 删除文件，支持通配符 | DeleteFileW, FindFirstFileW, FindNextFileW |
| copy | 复制文件 | CopyFileW |
| move / ren / rename | 移动或重命名 | MoveFileExW |
| type | 显示 UTF-8 文本文件 | CreateFileW, ReadFile, MultiByteToWideChar |

环境变量命令：

| 命令 | 功能 | API |
| --- | --- | --- |
| set | 枚举、查询、设置、删除环境变量 | GetEnvironmentStringsW, GetEnvironmentVariableW, SetEnvironmentVariableW |
| echo | 输出文本，展开 %CD%、%PATH%、%ERRORLEVEL% | GetEnvironmentVariableW |

控制台与辅助命令：

| 命令 | 功能 | API |
| --- | --- | --- |
| pwd | 显示当前目录 | GetCurrentDirectoryW |
| date / time | 显示日期和时间 | GetLocalTime |
| ver | 显示版本、用户名、计算机名 | GetUserNameW, GetComputerNameW |
| cls / clear | 清屏 | GetStdHandle, FillConsoleOutputCharacterW, SetConsoleCursorPosition |
| help / ? | 显示帮助 | 内部帮助输出 |

讲稿：
除了任务书要求的核心命令，我还扩展了文件管理、环境变量和辅助命令。文件管理命令主要使用 Windows 文件系统 API。set 和 echo 用于管理和显示环境变量。cls 和 clear 没有使用 system("cls")，而是直接使用控制台缓冲区 API 清屏，这样更符合内部命令直接调用 Windows API 的设计思想。


第 8 页：错误处理、资源管理与实现亮点

标题：
错误处理、资源管理与实现亮点

页面内容：
错误处理：

- Windows API 调用失败后调用 GetLastError
- 使用 FormatMessageW 转换为可读错误信息
- 参数不足、路径错误、PID 非法、权限不足都会提示
- 出错后 Shell 主循环继续运行

资源释放：

| 资源 | 释放方式 |
| --- | --- |
| 进程句柄 / 线程句柄 | CloseHandle |
| 快照句柄 | CloseHandle |
| 文件句柄 | CloseHandle |
| 查找句柄 | FindClose |
| 环境变量块 | FreeEnvironmentStringsW |
| 错误消息缓冲区 | LocalFree |

实现亮点：

- 完成任务书全部核心命令
- 内部命令直接调用 Win32 API
- 支持中文路径和带空格路径
- 支持外部命令执行和 ERRORLEVEL
- 进程管理功能完整
- 文件管理扩展较丰富
- cls 使用控制台 API 实现
- 模块化结构清晰，便于维护

讲稿：
为了保证程序稳定性，我对常见错误做了处理。API 调用失败后，通过 GetLastError 和 FormatMessageW 输出可读错误信息。同时，所有通过 Windows API 获取的句柄和资源都会及时释放。项目的亮点是核心命令完整、API 调用明确、模块划分清晰，并且扩展功能比较丰富。


第 9 页：测试展示

标题：
功能测试与演示命令

页面内容：
测试表：

| 测试内容 | 测试命令 | 预期结果 |
| --- | --- | --- |
| 当前目录 | cd / pwd | 正常显示当前目录 |
| 目录列表 | dir / dir . | 显示文件、目录和磁盘信息 |
| 历史记录 | history / history 2 | 显示历史命令 |
| 进程列表 | tasklist notepad | 筛选 notepad 进程 |
| 结束进程 | taskkill /IM notepad.exe /F | 结束记事本进程 |
| 文件管理 | mkdir / copy / type / del / rmdir | 文件操作正常 |
| 外部命令 | ipconfig / where cmd | 外部命令正常执行 |
| 退出码 | echo %ERRORLEVEL% | 显示上一条命令退出码 |
| 清屏 | cls / clear | 控制台正常清屏 |

现场演示建议：

```text
cd
dir
history
tasklist notepad
taskkill /IM notepad.exe /F
mkdir testdir
copy README.md testdir\a.txt
type testdir\a.txt
del testdir\a.txt
rmdir testdir
ipconfig
echo %ERRORLEVEL%
cls
exit
```

讲稿：
我对核心命令和扩展命令都进行了测试。测试内容包括目录操作、历史记录、进程列表、进程结束、文件管理、外部命令、退出码和清屏。程序能够完成预期功能，并且错误输入不会导致程序崩溃。


第 10 页：总结与不足

标题：
总结与不足

页面内容：
实训收获：

- 理解了命令解释器的基本工作流程
- 掌握了常用 Win32 API 的调用方式
- 理解了内部命令和外部命令的区别
- 学习了文件系统、进程管理、环境变量和控制台操作
- 提高了模块化设计和错误处理能力

不足与改进：

- 暂不支持管道和重定向
- 暂不支持脚本批处理
- type 主要支持 UTF-8 文本
- copy 不自动创建目标目录
- taskkill 使用 TerminateProcess 强制结束进程

结束语：

本项目完成了任务书要求的核心功能，并在文件管理、环境变量、控制台清屏和外部命令执行方面进行了扩展。通过本次实训，我对 Windows API、命令解释器设计和操作系统接口有了更深入的理解。

讲稿：
通过本次实训，我理解了命令解释器从读取命令、解析命令、分发命令到调用系统 API 的完整流程，也掌握了 Windows 文件、目录、进程和控制台相关 API 的使用。项目目前仍有一些不足，例如不支持管道、重定向和脚本批处理，后续可以继续扩展。以上就是我的汇报，谢谢各位老师。
