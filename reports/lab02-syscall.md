# Lab 2：系统调用

## 实验目的

理解 xv6 从用户态函数到内核系统调用处理函数的完整路径，掌握系统调用编号、参数传递、进程状态和用户/内核地址空间之间的数据复制。本实验实现 `trace` 和 `sysinfo` 两个系统调用。

## 实验环境

- 代码基线：MIT 6.S081 2021 `syscall` 分支。
- 目标架构：RISC-V 64 位，`rv64gc/lp64`。
- 主机系统：Windows 11，Linux 环境为 WSL2 + Ubuntu 22.04 LTS。
- 工具链：RISC-V 交叉编译器、GNU Make、GDB 和 QEMU。

所有编译、运行和测试命令均在 Ubuntu 终端中执行。Makefile 显式加入 `-march=rv64gc -mabi=lp64` 并指定 `elf64lriscv`，保证工具链生成符合 xv6 要求的 RV64 目标文件。

## 系统调用路径

以 `trace(mask)` 为例，调用依次经过：

1. `user/user.h` 声明用户态接口；
2. `user/usys.pl` 生成汇编桩，把系统调用编号放入 `a7` 并执行 `ecall`；
3. 陷阱处理进入内核，`kernel/syscall.c` 从陷阱帧中取得编号和参数；
4. 分发表按照 `SYS_trace` 调用 `sys_trace()`；
5. 返回值写回陷阱帧的 `a0`，用户程序恢复执行。

`sysinfo` 使用相同路径，但还需要用 `copyout()` 把内核中构造的结构体安全地复制到用户虚拟地址。

## `trace` 的实现

- 在 `struct proc` 中增加 `trace_mask`，每一位对应一个系统调用编号。
- `sys_trace()` 读取用户参数并更新当前进程的掩码。
- `fork()` 把父进程的掩码复制给子进程，使追踪设置能够继承。
- `allocproc()` 和 `freeproc()` 都清零掩码，防止复用进程槽位时继承上一个进程的设置。
- 系统调用处理完成后，根据掩码打印 PID、系统调用名和返回值；只有目标位被设置时才输出。

输出格式为：

```text
pid: syscall name -> return_value
```

## `sysinfo` 的实现

`sysinfo` 返回两个统计值：

- `freemem`：在持有 `kmem.lock` 时遍历空闲页链表，以空闲页数乘 `PGSIZE` 得到空闲字节数。
- `nproc`：逐个持有进程锁，统计状态不是 `UNUSED` 的进程槽位。

内核先在栈上构造 `struct sysinfo`，再使用当前进程页表调用 `copyout()`。如果用户地址无效，系统调用返回 `-1`，从而避免直接解引用用户指针导致内核错误。

## 实现检查与边界处理

实现过程中重点检查了以下三个并发与状态边界：

1. 遍历物理页空闲链表时持有分配器锁，避免与并发分配或释放发生数据竞争；
2. 读取进程状态时持有对应进程锁；
3. 分配和释放进程槽位时都重置追踪掩码，避免旧状态泄漏到新进程。

## 遇到的问题与解决方法

### 1. 追踪状态的继承与清理

`trace_mask` 需要在 `fork()` 中传给子进程，同时在进程槽位分配和释放时清零。否则子进程不能继承追踪设置，或新进程可能错误沿用旧进程的掩码。实现中分别在 `fork()`、`allocproc()` 和 `freeproc()` 中处理这些状态转换。

### 2. 统计共享内核状态的一致性

直接遍历空闲页链表或读取进程状态虽然可能通过简单测试，但在多核 xv6 中会与其他 hart 并发修改。解决方法是遵守原有数据结构的锁规则，在最小必要范围内持锁完成统计。

### 3. 用户指针不能在内核中直接使用

用户传入的地址可能未映射或没有写权限。实现中使用 `argaddr()` 获取地址，再由 `copyout()` 按用户页表检查并复制；复制失败时返回 `-1`。

## 实验结果

手动启动 QEMU 后运行：

```text
$ trace 32 grep hello README
3: syscall read -> 1023
3: syscall read -> 968
3: syscall read -> 235
3: syscall read -> 0
$ grep hello README
$ sysinfotest
sysinfotest: start
sysinfotest: OK
```

运行 MIT `grade-lab-syscall`，最终结果为：

```text
trace 32 grep: OK
trace all grep: OK
trace nothing: OK
trace children: OK
sysinfotest: OK
time: OK
Score: 35/35
```

## 实验心得

本实验把“用户函数调用”与“受控进入内核”连接起来：系统调用不是普通函数跳转，而是通过寄存器、陷阱和分发表进入内核。`trace` 说明进程控制块保存了跨系统调用的进程级状态；`sysinfo` 则说明读取全局状态也必须遵守并发锁规则，并通过 `copyout()` 跨越用户/内核边界。
