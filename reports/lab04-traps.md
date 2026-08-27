# Lab 4：陷阱与用户级中断

## 实验目的

理解 RISC-V 函数调用约定、栈帧链、用户态陷阱进入内核及返回用户态的过程，完成：

1. 阅读 `call.asm` 并回答 RISC-V 汇编问题；
2. 在内核中实现栈回溯 `backtrace()`；
3. 实现 `sigalarm`/`sigreturn`，让进程按其消耗的 CPU ticks 周期执行用户态处理函数。

## 实验环境

- 代码基线：MIT 6.S081 2021 `traps` 分支；
- 目标架构：RISC-V 64 位，`rv64gc/lp64`；
- 主机系统：Windows 11，Linux 环境为 WSL2 + Ubuntu 22.04 LTS；
- 工具链：RISC-V 交叉编译器、GNU Make、GDB 和 QEMU。

所有编译、运行和测试命令均在 Ubuntu 终端中执行，Makefile 明确使用 RV64 架构和 `lp64` ABI。

## RISC-V 汇编观察

实际生成的 `user/call.asm` 表明：

- 前八个整数/指针参数使用 `a0` 到 `a7`；13 位于 `a2`；
- `g` 被内联进 `f`，`main` 中的 `f(8)+1` 又被常量折叠为 12；
- 本工具链生成的 `printf` 地址为 `0x64e`；
- 调用 `printf` 的 `jalr` 位于 `0x3c`，所以返回地址 `ra` 为 `0x40`；
- RISC-V 小端内存把 `0x00646c72` 排列为 `72 6c 64 00`，即 `"rld"`。

完整回答保存在 `answers-traps.txt`。汇编地址会随编译器版本和最终链接内容变化，因此应以当前构建生成的 `call.asm` 为准进行解释，不使用固定的旧地址。

## `backtrace` 的实现

RISC-V ABI 使用 `s0` 作为帧指针。每个已建立的内核栈帧中：

- `fp - 8` 保存返回地址；
- `fp - 16` 保存调用者帧指针。

新增 `r_fp()` 读取 `s0`，`backtrace()` 从当前帧开始沿保存的帧指针向调用者遍历。每个 xv6 内核栈只有一页，因此以当前帧所在页的上下边界限制访问，并要求调用者帧地址单调增大，避免损坏的帧链造成越界或死循环。

按照实验要求，在 `sys_sleep()` 中调用 `backtrace()`。`bttest` 输出的三个地址经 `addr2line` 解析为：

```text
kernel/sysproc.c
kernel/syscall.c
kernel/trap.c
```

## alarm 状态设计

在每个 `struct proc` 中保存：

- `alarm_interval`：两次 alarm 之间的用户态 timer ticks；
- `alarm_ticks`：当前累计 tick；
- `alarm_handler`：用户处理函数虚拟地址；
- `alarm_active`：处理函数是否正在执行；
- `alarm_frame`：被中断时完整的 `struct trapframe`。

这些字段在 `allocproc()` 和 `freeproc()` 中都被清零，避免进程槽位复用时继承旧状态。新 fork 出的进程从无 alarm 配置开始。

## `sigalarm` 与定时触发

`sigalarm(interval, handler)` 保存 interval 和用户函数地址，并把 tick 计数归零。interval 为 0 时停止未来触发；如果当前正在 handler 中，不会提前清除 `alarm_active`，因为 handler 仍需调用 `sigreturn()` 恢复中断现场。

用户态定时中断进入 `usertrap()` 后：

1. 仅在 interval 大于 0 且 handler 未运行时累计 tick；
2. 达到用户指定的 interval 后复制完整 trapframe；
3. 清零计数并设置 `alarm_active`；
4. 把当前 trapframe 的 `epc` 改为 handler 地址；
5. 正常 `yield()`，进程再次运行时从 handler 开始。

`alarm_active` 阻止慢 handler 被再次打断并重入。

## `sigreturn` 的实现

handler 调用 `sigreturn()` 后，内核把保存的 trapframe 整体恢复，并清除 `alarm_active`。系统调用返回值必须使用被中断现场原有的 `a0`；否则 `syscall()` 会在返回时覆盖恢复后的 `a0`，破坏用户寄存器状态。

完整恢复不仅包括 `epc` 和栈指针，也包括循环变量可能使用的所有通用寄存器。`alarmtest` 的 test1 会通过比较循环次数专门检查这一点。

## 实现检查与边界处理

实现过程中重点检查并处理了以下细节：

1. 使用 `alarm_interval` 作为实际阈值，而不是把触发条件写死为 2；
2. 在分配与释放进程槽位时对全部 alarm 状态进行对称初始化；
3. `sigreturn` 只允许在 handler 活跃时恢复，并保留原 `a0`；
4. handler 执行期间不累计或嵌套 alarm；
5. backtrace 同时检查栈页边界和帧指针单调性；
6. 不修改 `FSSIZE`，官方 starter 的文件系统大小足以通过全部测试。

## 遇到的问题与解决方法

### 1. 错把 handler 地址 0 当成无效指针

最初为 `sigalarm` 增加了常见的空指针检查，结果 test0 和 test1 始终不触发，而使用 `slow_handler` 的 test2 可以触发。计时验证表明前两组循环分别运行数秒和约 30 秒，不是循环过快。

检查 `alarmtest.asm` 后发现 `periodic` 正好被链接在用户虚拟地址 `0x0`。xv6 会把用户程序从地址 0 开始映射，因此 0 在这里是合法代码地址，不能套用现代操作系统“零页不映射”的经验。去掉 `handler == 0` 拒绝条件后，三组测试全部通过。

### 2. 禁用 alarm 不能破坏当前 handler 的返回现场

`slow_handler` 会依次调用 `sigalarm(0, 0)` 和 `sigreturn()`。如果禁用时同时清除 `alarm_active`，后续 `sigreturn` 就无法恢复现场。解决方法是只关闭未来触发，当前 handler 状态由 `sigreturn` 结束。

### 3. backtrace 必须恰好停在当前内核栈内

只判断帧指针是否等于页边界对损坏栈不够稳健。本实现固定记录初始内核栈页的上下界，并检查下一帧必须向高地址移动且仍在该页内。

## 实验结果

手动运行：

```text
$ bttest
backtrace:
0x00000000800021fc
0x00000000800020d8
0x0000000080001d72
$ alarmtest
test0 passed
...
test1 passed
...
test2 passed
```

运行 MIT `grade-lab-traps`，结果为：

```text
answers-traps.txt: OK
backtrace test: OK
alarmtest: test0: OK
alarmtest: test1: OK
alarmtest: test2: OK
usertests: OK
time: OK
Score: 85/85
```

完整 `usertests` 运行约 281 秒，说明新增进程状态和陷阱返回逻辑没有破坏既有内核行为。

## 实验心得

alarm 本质上是一个简化的用户态信号机制：内核把正常执行现场保存起来，构造一次到用户 handler 的返回，再由特殊系统调用恢复原现场。该过程把汇编调用约定、陷阱帧、系统调用返回值和进程状态联系起来，也说明了完整保存 trapframe、禁止 handler 重入和正确处理用户虚拟地址的重要性。
