# Lab 6：多线程

## 实验目的

从三个层次理解线程与同步：

1. 在 xv6 用户态实现协作式线程的上下文切换；
2. 使用 pthread mutex 修复并行哈希表的 lost update，并保留并行度；
3. 使用 mutex 与 condition variable 实现可重复使用的 barrier。

## 实验环境

- 代码基线：MIT 6.S081 2021 `thread` 分支；
- xv6 部分：RISC-V 64 位交叉编译后在 QEMU 9.2.4 中运行；
- pthread 部分：Windows 11、MinGW-w64 POSIX threads；
- 正式答辩环境计划：WSL 2 + Ubuntu，pthread 源码可直接编译。

宿主程序把 POSIX 扩展 `random/srandom` 改为标准 C `rand/srand`，线程编号使用 `intptr_t` 在整数和 `void *` 之间转换，以同时适应 Linux LP64 和 Windows LLP64 数据模型，不改变并发算法。

## 用户态线程结构

每个线程有独立的 8192 字节栈、状态和上下文。上下文保存：

```text
ra, sp, s0-s11
```

`thread_create()` 清零新上下文，把 `ra` 设为线程入口函数，把 `sp` 设为线程栈顶并向下对齐到 16 字节，满足 RISC-V ABI。若没有空闲槽位则明确报错，避免越界写入线程数组。

## `thread_switch` 的实现

调度器先把目标线程设为 `RUNNING`，把当前线程指针切换到目标，再调用：

```c
thread_switch(&old->context, &new->context);
```

汇编函数先把旧线程的 `ra/sp/s0-s11` 保存到 `a0` 指向的结构，再从 `a1` 指向的结构恢复新线程寄存器，最后执行 `ret`。

- 首次运行新线程时，恢复的 `ra` 是线程入口，因此 `ret` 相当于启动该函数；
- 恢复旧线程时，`ra` 指向它上次调用 `thread_switch` 后的位置，因此继续完成原来的 `thread_schedule()`。

只需显式保存 callee-saved 寄存器，是因为 C 编译器在普通函数调用点已经按 ABI 假设 caller-saved 寄存器可以被破坏，并负责保存仍然活跃的值。

## 并发哈希表

原版 `put()` 的问题是多个线程可以同时读取同一个 bucket 旧表头，然后分别把自己的新节点写入 `table[i]`；最后一次写入覆盖前一次，导致一个节点永久丢失。

实现为每个 bucket 分配一个 mutex，并把以下步骤放在同一个临界区：

1. 遍历 bucket 查找 key；
2. 更新已有 entry，或分配并链接新 entry；
3. 解锁。

锁必须在查找前获取。如果像学长版本那样先无锁查找、再在插入前加锁，两个相同 key 仍可能同时被判断为不存在，也不能保证整个判断—修改序列原子化。

不同 bucket 使用不同锁，因此散列到不同 bucket 的 `put()` 能并行执行。所有 put 线程 join 后才启动只读 get 线程，join 建立同步关系，get 阶段无需继续获取 bucket 锁。

## 可复用 Barrier

barrier 状态包括：

- `nthread`：本轮已经到达的线程数；
- `round`：当前 generation；
- mutex 和 condition variable。

线程进入 barrier 后持锁记录当前轮次并增加人数：

- 最后到达者把人数重置为 0、增加 round，并 broadcast 唤醒本轮所有等待者；
- 其他线程使用 `while (this_round == bstate.round)` 调用 `pthread_cond_wait()`。

必须使用 `while` 而不是单次 `if`：condition variable 允许虚假唤醒；同时最快的线程可能很快进入下一轮，等待条件必须以 generation 是否改变为准。`pthread_cond_wait` 在睡眠时原子释放 mutex，醒来后重新获得，从而不会错过最后到达者的状态更新。

## 与学长实现的核对

学长仓库用于确认实验流程，本实现加强了：

1. 新线程栈显式按 16 字节对齐，且复用槽位时清零旧上下文；
2. 无空闲线程槽时避免数组越界；
3. ph 的锁覆盖查找与修改整个原子操作，而不是查找后才加锁；
4. barrier 使用轮次条件的 `while` 循环，处理虚假唤醒与跨轮竞速；
5. 正确销毁 mutex/condition variable 并释放宿主线程数组；
6. 使用 `intptr_t` 避免 Windows 64 位指针被 32 位 long 截断。

## 遇到的问题与解决方法

### 1. Windows pthread 源码的类型与随机数接口

MinGW 环境没有 `random/srandom`，Windows 64 位 ABI 中 `long` 仍是 32 位。原代码把线程编号经 long 转为指针会产生尺寸警告，也不是通用写法。改用 C 标准 `rand/srand` 和 `<stdint.h>` 中保证能容纳指针的 `intptr_t` 后，Linux 与 Windows 都可编译。

### 2. 官方评分正则与 CRLF

宿主二进制实际输出正确，但第一次 Windows 评分中 ph 和 barrier 被统计为没有匹配项。捕获原始字节后发现 C 运行库使用 `\r\n`，MIT 脚本的正则以 `$` 严格匹配 Linux LF。未提交的 Windows 评分副本只把捕获文本中的 CRLF 规范化为 LF，再使用原始正则和性能门槛，最终得到 60/60。在 WSL/Linux 中无需这一兼容层。

### 3. 正确性与并行度的权衡

一个全局锁能够消除 key 丢失，但几乎串行化所有 put，无法通过 ph_fast。每 bucket 一把锁使相同链表的操作串行、不同链表的操作并行，锁粒度与数据分区一致。

## 实验结果

QEMU 中 `uthread` 严格输出：

```text
thread_a started
thread_b started
thread_c started
thread_c 0
thread_a 0
thread_b 0
...
thread_c 99
thread_a 99
thread_b 99
thread_c: exit after 100
thread_a: exit after 100
thread_b: exit after 100
thread_schedule: no runnable threads
```

barrier 分别以 1、2、4 线程执行 20,000 轮，均输出 `OK; passed`。

ph 连续三组测速：

```text
1 thread: 47494 puts/s; 2 threads: 98054 puts/s; speedup 2.06x
1 thread: 61436 puts/s; 2 threads: 103908 puts/s; speedup 1.69x
1 thread: 61959 puts/s; 2 threads: 99347 puts/s; speedup 1.60x
```

所有两线程运行的两个 get 线程都报告 `0 keys missing`。

MIT `grade-lab-thread` 最终结果：

```text
uthread: OK
answers-thread.txt: OK
ph_safe: OK
ph_fast: OK
barrier: OK
time: OK
Score: 60/60
```

## 心得与答辩准备

上下文切换保存的是“让一段执行未来能继续”的最小机器状态；mutex 保护的是跨多条语句的数据不变量；condition variable 则把“等待某个状态变化”与锁结合起来。答辩时应能解释 `ret` 如何启动新线程、caller/callee-saved 的区别、lost update 的具体交错、为什么每 bucket 锁兼顾正确与性能，以及 barrier 为什么必须记录 round 并在循环中等待。
