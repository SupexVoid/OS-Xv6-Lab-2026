# Lab 1：Xv6 与 Unix 实用程序

## 实验目的

熟悉 xv6 用户程序的编译与运行方式，练习进程、管道、文件描述符、目录项与 `exec` 等系统调用，完成 `sleep`、`pingpong`、`primes`、`find` 和 `xargs`。

## 实现要点

- `sleep`：检查 ticks 参数，使用已有的 `sleep` 系统调用暂停进程。
- `pingpong`：建立两个单向管道，父子进程各关闭不使用的端点，只交换一个字节并输出各自 PID。
- `primes`：父进程写入 2 到 35；每一级筛选进程打印当前首个素数，把不能被它整除的整数传给下一级，并通过关闭写端传播 EOF。
- `find`：参考 `user/ls.c` 解析目录项，跳过 `.` 和 `..`，递归遍历目录并按文件名匹配。
- `xargs`：逐字符读取标准输入，以换行划分任务、空白划分参数，为每一行 `fork` 子进程后调用 `exec`，父进程等待子进程结束。

## 与参考实现的核对

学长仓库用于确认实验流程和涉及文件，但本分支从 MIT 官方 `util` starter 开始重新实现。核对时发现参考代码有两个不适合直接复制的细节：

1. xv6 的 `gets()` 即使读到 EOF 也返回传入的缓冲区地址，直接用 `while (gets(...))` 可能无法正常结束；本实现改用 `read()` 的返回值判断 EOF。
2. 管道端点若没有及时关闭，会让 EOF 无法到达或浪费有限的文件描述符；本实现对父、子进程各自不使用的端点逐一关闭。

## 遇到的问题与解决方法

### 1. Windows 尚未安装 WSL

为先验证代码，使用项目外的便携 RISC-V GCC 与 QEMU；正式答辩仍计划使用 MIT 推荐的 WSL 2 + Ubuntu 环境。

### 2. 新版交叉编译器默认 RV32

GCC 15 对未显式指定架构的汇编采用了不符合 xv6 的默认 ABI。Makefile 中统一指定 `-march=rv64gc -mabi=lp64`，链接器显式使用 `elf64lriscv`。

### 3. Windows 文本/二进制文件语义不同

宿主机 `mkfs` 若以文本模式读取 ELF 文件，会把某些字节解释为 EOF 或换行。为镜像及输入文件增加 `O_BINARY`，并把已过时的 `bzero`、`bcopy`、`index` 替换为标准 C 函数。

## 实验结果

RISC-V 内核、文件系统镜像及五个用户程序均成功交叉编译。QEMU 冷启动后实际运行 `pingpong`、`primes`、`xargs` 和递归 `find`，输出符合题目要求。

MIT `grade-lab-util` 的测试项目保持不变，仅对 Windows 管道轮询方式使用未提交的临时兼容层，结果为：

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

## 心得与答辩准备

本实验的关键不是五个程序本身的代码量，而是理解文件描述符会被 `fork` 复制、管道 EOF 依赖所有写端关闭，以及 xv6 用户态只能使用有限的库函数。答辩时应能解释 `primes` 为什么需要多个进程、`find` 为什么必须跳过 `.`/`..`、`xargs` 如何知道输入结束。
