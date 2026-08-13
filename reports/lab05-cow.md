# Lab 5：Copy-on-Write Fork

## 实验目的

理解 `fork()` 复制地址空间的成本、页故障处理和共享物理页的生命周期管理，实现写时复制（Copy-on-Write，COW）：父子进程最初共享物理页，只有某一方真正写入时才复制。

## 实验环境

- 代码基线：MIT 6.S081 2021 `cow` 分支；
- 目标架构：RISC-V 64 位，`rv64gc/lp64`；
- 当前验证环境：Windows 11、便携 RISC-V GCC 15.2.0、QEMU 9.2.4；
- 正式答辩环境计划：WSL 2 + Ubuntu。

Makefile 和 `mkfs` 继续使用 RV64 及 Windows 二进制模式兼容修改，不改变 COW 的实验逻辑。

## 原始 `fork` 的问题

原版 `uvmcopy()` 为子进程的每个用户页调用 `kalloc()`，再完整复制父进程数据。如果父进程占用超过一半物理内存，子进程即使只想马上 `exec()`，`fork()` 也会因无法再分配同样多的页而失败。大量复制还可能完全没有被使用。

COW 把复制推迟到写入发生时：读操作可以一直共享，`fork()` 后马上 `exec()` 的子进程可以直接丢弃共享映射，不需要复制原地址空间。

## COW 页表标记

使用 RISC-V PTE 中预留给软件的 RSW 位之一定义 `PTE_COW`。

`uvmcopy()` 对每个用户页执行：

1. 如果父 PTE 原本有 `PTE_W`，同时清除写权限并设置 `PTE_COW`；
2. 真正只读的代码页不设置 COW，防止写故障后错误地变成可写；
3. 子页表映射同一物理页和相同标志；
4. 为新增的子 PTE 增加物理页引用计数。

如果子页表映射中途失败，已建立的映射通过 `uvmunmap(..., 1)` 回滚并减少引用数。父页可能保持为只有一个引用的 COW 映射，但语义仍正确：下次写入可直接恢复写权限。修改父页表写权限后执行 `sfence.vma`，防止处理器继续使用旧的可写 TLB 项。

## 物理页引用计数

每个可分配物理页有一个引用计数，数组以 `(pa - KERNBASE) / PGSIZE` 索引，并由独立的 `krefs.lock` 保护。

- 初始化空闲链表：临时把引用数设为 1，再通过统一的 `kfree()` 降到 0；
- `kalloc()`：从 freelist 取出页后把引用数设为 1；
- fork 共享：`krefinc()` 增加一个 PTE 引用；
- 解除映射：原有 `kfree()` 先减少引用数；
- 只有计数降为 0 时，才填充垃圾数据并把物理页放回 freelist。

引用数为负或对空闲页增加引用都触发内核 panic，因为这代表内核自身的所有权不变量被破坏，而不是普通用户输入错误。

引用计数实现封装在 `kalloc.c` 中，页表代码只能通过 `krefinc()` 和 `krefcount()` 操作，避免像直接暴露全局数组那样绕过锁。

## 写故障与 `cowcopy`

用户态写入只读 COW 页时，RISC-V 产生 store/AMO page fault（`scause == 15`），`stval` 保存故障虚拟地址。`usertrap()` 先确认地址位于进程大小内，再调用统一的 `cowcopy()`。

`cowcopy()` 验证 PTE 同时具有 `PTE_V`、`PTE_U` 和 `PTE_COW`：

- 引用数为 1：已经没有其他页表共享物理页，只需清除 COW 位并恢复 `PTE_W`，无需复制；
- 引用数大于 1：分配新页、复制 4096 字节、让当前 PTE 指向新页并恢复写权限，最后减少旧页引用；
- 无可用内存或不是合法 COW 页：返回失败，用户写故障路径杀死进程。

更新 PTE 后执行 `sfence.vma`，使当前 hart 不再使用旧的只读翻译。

## `copyout` 的 COW 兼容

文件读取、管道读取、`wait` 状态复制等路径由内核直接调用 `copyout()` 写用户内存，不会产生用户态页故障。如果 `copyout()` 直接通过物理地址写共享页，会同时修改父子进程数据。

因此 `copyout()` 在写每一页前：

1. 检查地址低于 `MAXVA`；
2. 验证 PTE 有效且用户可访问；
3. 如果是 COW 映射，调用同一个 `cowcopy()`；
4. 重新读取 PTE，并确认最终具有写权限；
5. 再向新的物理页复制数据。

这样用户写故障和内核写用户内存共享完全相同的 COW 语义，真正只读页也不会被 `copyout()` 绕过权限修改。

## 与学长实现的核对

学长仓库用于确认实验流程和涉及文件，本实现额外加强了：

1. 引用计数数组与锁封装在分配器中，不由 `vm.c` 直接读写全局变量；
2. 引用数为 1 时原地恢复写权限，避免不必要的分配和复制；
3. fork 映射失败时撤销本次引用，并对父 TLB 做刷新；
4. `copyout()` 对错误返回 `-1`，不随意设置当前进程 killed 状态；
5. `copyout()` 显式检查用户页最终确实可写；
6. 用户页故障只接受 store page fault 和真正的 `PTE_COW`，原始只读代码页仍不可写。

## 遇到的问题与解决方法

### 1. 改写 `copyout` 时遗漏 `MAXVA` 防护

第一次完整评分中，COW 的 simple、three、file 全部通过，`usertests` 的 copyin 也通过，但 copyout 在处理 `0xffffffffffffffff` 时触发 `panic: walk`。

原因是原版 `copyout()` 使用 `walkaddr()`，后者会在调用 `walk()` 前拒绝 `MAXVA` 以上地址；COW 版本为了检查 PTE 标志而直接调用 `walk()`，却没有把这一前置条件一起迁移。`walk()` 把超范围地址视为内核编程错误并 panic。

解决方法是在页对齐后先判断 `va0 >= MAXVA` 并返回 `-1`。单独运行 `usertests copyout` 通过后，再次完整评分得到 110/110。这说明重构调用路径时不能只替换主逻辑，还要保留被旧辅助函数隐含提供的边界保证。

### 2. allocator 初始化时引用数仍为 0

`freerange()` 使用 `kfree()` 把启动时的全部物理页加入 freelist，但统一后的 `kfree()` 会先减少引用数。若直接从 0 减一会触发引用数下溢。初始化时先为每页建立一个临时所有权（计数 1），再调用正常释放路径变为 0，从而不需要为启动阶段保留另一套释放语义。

### 3. 父进程旧 TLB 可能仍允许写入

只清除内存中父 PTE 的 `PTE_W` 不足以保证处理器立即停止使用已有 TLB 翻译。fork 完成或失败回滚后调用 `sfence.vma`，确保后续写入真正产生 COW page fault。

## 实验结果

手动运行 `cowtest`：

```text
simple: ok
simple: ok
three: ok
three: ok
three: ok
file: ok
ALL COW TESTS PASSED
```

MIT `grade-lab-cow` 测试内容保持不变，仅使用未提交的 Windows 进程和管道兼容层启动 QEMU。修复边界问题后的完整结果为：

```text
simple: OK
three: OK
file: OK
usertests: copyin: OK
usertests: copyout: OK
usertests: all tests: OK
time: OK
Score: 110/110
```

完整 `usertests` 运行约 247 秒，说明引用计数在大量 fork、exec、exit、sbrk 和内存回收后仍保持一致。

## 心得与答辩准备

COW 的难点不在复制一页，而在维护“每个 PTE 引用都对应一个引用计数”的全局不变量。页表权限用于让硬件把首次写入转化为内核可处理的事件，引用计数决定物理页何时真正释放，TLB 刷新保证权限修改立即生效，`copyout` 则提醒我们内核访问用户内存不会自动经过用户态 fault 路径。答辩时应能画出 fork 共享、父/子首次写入、子进程退出三种状态变化，并解释只读代码页为什么不能标记为 COW。
