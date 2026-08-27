# Lab 1：Xv6 与 Unix 实用程序

## 实验目的

熟悉 xv6 用户程序的编译与运行方式，练习进程、管道、文件描述符、目录项与 `exec` 等系统调用，完成 `sleep`、`pingpong`、`primes`、`find` 和 `xargs`。

## 实验环境

- 主机系统：Windows 11；
- Linux 环境：WSL2 + Ubuntu 22.04 LTS；
- 工具链：RISC-V 交叉编译器、GNU Make、GDB 和 QEMU。

所有编译、运行和测试命令均在 Ubuntu 终端中执行。

## 实现要点

- `sleep`：检查 ticks 参数，使用已有的 `sleep` 系统调用暂停进程。
- `pingpong`：建立两个单向管道，父子进程各关闭不使用的端点，只交换一个字节并输出各自 PID。
- `primes`：父进程写入 2 到 35；每一级筛选进程打印当前首个素数，把不能被它整除的整数传给下一级，并通过关闭写端传播 EOF。
- `find`：参考 `user/ls.c` 解析目录项，跳过 `.` 和 `..`，递归遍历目录并按文件名匹配。
- `xargs`：逐字符读取标准输入，以换行划分任务、空白划分参数，为每一行 `fork` 子进程后调用 `exec`，父进程等待子进程结束。

## 实现检查与边界处理

实现过程中重点检查了两个容易导致程序阻塞或无法退出的细节：

1. xv6 的 `gets()` 即使读到 EOF 也返回传入的缓冲区地址，直接用 `while (gets(...))` 可能无法正常结束；本实现改用 `read()` 的返回值判断 EOF。
2. 管道端点若没有及时关闭，会让 EOF 无法到达或浪费有限的文件描述符；本实现对父、子进程各自不使用的端点逐一关闭。

## 遇到的问题与解决方法

### 1. 交叉编译器 ABI 配置

部分新版交叉编译器不会自动选择 xv6 所需的 64 位 ABI。Makefile 中统一指定 `-march=rv64gc -mabi=lp64`，链接器使用 `elf64lriscv`，确保内核与用户程序均按 RV64 目标生成。

### 2. 管道写端未关闭导致 EOF 无法到达

`pingpong` 和 `primes` 中如果父子进程保留了不使用的写端，读取方会一直等待。解决方法是在 `fork` 后立即关闭各自不用的端点，并在写入完成后关闭写端，让 EOF 沿管道正确传播。

### 3. `xargs` 的输入结束判断

xv6 的用户库接口较精简，直接依赖 `gets()` 的返回指针不能可靠区分 EOF。本实现逐字符调用 `read()`，按空白划分参数、按换行启动子进程，并依据返回值结束输入循环。

## 实验结果

RISC-V 内核、文件系统镜像及五个用户程序均成功交叉编译。QEMU 冷启动后实际运行 `pingpong`、`primes`、`xargs` 和递归 `find`，输出符合题目要求。

运行 MIT `grade-lab-util`，结果为：

```text
sleep, no arguments: OK
sleep, returns: OK
sleep, makes syscall: OK
pingpong: OK
primes: OK
find, in current directory: OK
find, recursive: OK
xargs: OK
time: OK
Score: 100/100
```

## 实验心得

本实验的关键不是五个程序本身的代码量，而是理解文件描述符会被 `fork` 复制、管道 EOF 依赖所有写端关闭，以及 xv6 用户态只能使用有限的库函数。通过实现 `primes`、`find` 和 `xargs`，进一步掌握了进程协作、目录递归与标准输入解析的基本方法。
