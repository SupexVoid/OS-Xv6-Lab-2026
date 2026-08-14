# Lab 9：文件系统

## 实验目的

1. 在不改变磁盘 inode 大小的前提下加入双重间接块，把最大文件扩展到 65,803 个数据块；
2. 让块映射和截断释放保持完全对称，避免大文件删除后的空间泄漏；
3. 实现 `symlink(target, path)`、新的 inode 类型和 `O_NOFOLLOW`；
4. 理解 `open` 中路径解析、inode 锁、引用计数和日志事务的配合。

## 实验环境

- 代码基线：MIT 6.S081 2021 `fs` 分支；
- 交叉编译器：xPack RISC-V GCC 15.2.0；
- 虚拟机：QEMU 9.2.0，`virt`、1 个 hart、128 MiB 内存；
- 文件系统镜像：200,000 个 1 KiB block，共 204,800,000 字节；
- 主机：Windows 11；正式答辩环境计划使用 WSL 2 + Ubuntu。

`mkfs` 的实际布局输出为：

```text
nmeta 70 (boot, super, log blocks 30 inode blocks 13, bitmap blocks 25)
blocks 199930 total 200000
```

## 大文件 inode 布局

磁盘 inode 原本含 13 个地址槽：12 个直接地址和 1 个单间接地址。为了保持 `struct dinode` 为 64 字节，本实现不增加槽位，而是调整为：

```text
addrs[0..10]  -> 11 个直接数据块
addrs[11]     -> 1 个单间接块 -> 256 个数据块
addrs[12]     -> 1 个双重间接块
                 -> 256 个单间接块
                 -> 每个再指向 256 个数据块
```

因此：

```text
MAXFILE = 11 + 256 + 256 * 256 = 65803 blocks
```

内存 inode 与磁盘 inode 都声明 `addrs[NDIRECT+2]`，元素总数仍是 13；`mkfs` 的尺寸断言和内核编译均通过。

## `bmap` 的双重间接映射

`bmap(ip, bn)` 依次处理三段逻辑块号：

1. `bn < 11`：直接读取或分配 `ip->addrs[bn]`；
2. 减去 11 后 `bn < 256`：经 `ip->addrs[11]` 查单间接地址；
3. 再减去 256 后 `bn < 65536`：
   - `outer = bn / 256` 选择双重间接根中的第几个单间接块；
   - `inner = bn % 256` 选择该单间接块中的数据块地址。

双重间接根、第二层单间接块和最终数据块都只在首次需要时分配。任何地址数组更新都调用 `log_write()` 纳入文件系统日志；每次 `bread()` 都在退出路径上对应 `brelse()`，包括分配失败路径。

## `itrunc` 的对称释放

截断顺序为：

1. 释放 11 个直接数据块；
2. 遍历单间接块，释放其数据块，再释放单间接块本身；
3. 遍历双重间接根的 256 个入口：读取每个存在的第二层间接块，释放其中所有数据块，再释放第二层块；
4. 释放双重间接根并清零 inode 地址槽；
5. 把文件大小清零并 `iupdate()`。

完整 `usertests` 的 `writebig` 会按新 `MAXFILE` 写满 65,803 块、逐块读回并执行 `unlink("big")`。该测试通过，说明分配、查找和三层释放路径都实际工作，而不只是 `bigfile` 能写入。

## 符号链接系统调用

新增内容包括：

- syscall 号 `SYS_symlink = 22` 及用户态桩；
- inode 类型 `T_SYMLINK`；
- 打开标志 `O_NOFOLLOW = 0x004`；
- `sys_symlink()` 和 `open` 中的链接跟随逻辑；
- 构建目标 `user/_symlinktest`。

`sys_symlink(target, path)` 不要求 target 已存在。它在一次日志事务中创建 `T_SYMLINK` inode，并把 target 字符串连同结尾 `\0` 写入 inode 数据，避免以后解析时读取未初始化的内核栈字节。

## `open` 中的链接跟随

普通 `open` 遇到 symlink 时迭代执行：

1. 在当前 inode 锁保护下检查记录长度并读出完整目标；
2. `iunlockput()` 释放当前 inode 的锁和引用；
3. 对目标调用 `namei()`，获得新 inode 后加锁；
4. 若仍是 symlink 则继续，直到非链接 inode。

最多跟随 10 层。链路形成环时最终达到深度上限并返回 `-1`，所有失败路径都释放当前 inode。指定 `O_NOFOLLOW` 时跳过跟随，文件描述符直接引用 symlink inode，因此 `fstat` 能观察到 `T_SYMLINK`。

目录写权限和设备号检查放在完成跟随后执行，保证最终目标才决定打开语义。`create()` 也允许 `O_CREATE` 打开已存在的 symlink，再按是否带 `O_NOFOLLOW` 决定跟随；`link` 和 `unlink` 没有改动，仍操作链接 inode 本身。

## 与学长实现的核对

学长仓库用于核对地址槽布局、系统调用接线和测试流程。本实现加强了：

1. symlink 目标连同 NUL 一起持久化，并在读取时验证 inode 大小和终止符；
2. 跟随失败、悬空目标和深度超限路径统一释放 inode 锁与引用；
3. 目录访问模式检查延后到最终目标，且只检查实际写标志，不会把 `O_NOFOLLOW` 误当写入；
4. `O_CREATE` 遇到已有 symlink 时可以继续进入标准跟随逻辑；
5. 双重间接层分配失败时先释放已经读取的 buffer；
6. `itrunc` 完整释放数据块、第二层索引块和双重间接根；
7. symlinktest 仅在 `LAB=fs` 时加入镜像，保持其他独立分支最小化。

## 遇到的问题与解决方法

### 1. 修改 `NDIRECT` 后必须重建镜像

虽然地址槽总数仍为 13，但 `mkfs` 和内核对槽位语义的解释已经变化。使用旧 `fs.img` 会让同一槽位被当成不同层级。执行 `make clean` 并重新生成 200,000 块镜像后，磁盘与内核格式一致。

### 2. Windows 快照 I/O 使 `bigfile` 超时

QEMU 临时 snapshot 后端在 Windows 上写入很慢：180 秒时已持续前进到约 40,500 块，但尚未结束。改为对可随时重建的生成镜像直接测试，并使用 QEMU `cache=unsafe` 后，完全相同的 xv6 内核在 139.8 秒内写完并读回全部 65,803 块，低于官方 180 秒限制。之后重新生成干净镜像再跑回归，不把加速参数或测试数据提交。

### 3. symlink 循环不能无限递归

`b -> a`、`a -> b` 会让朴素递归耗尽内核栈。实现采用迭代跟随并限制 10 层；每跳都先释放旧 inode，再查找新目标，因此循环只会返回打开失败，不会泄漏锁或引用。

### 4. 最终目标的类型检查时机

若在跟随前只检查初始 inode，链接到只读目录或非法设备时可能绕过原有约束。把目录模式和设备号检查放到跟随完成后，普通文件、链接和链接链共享同一套最终验证。

## 实验结果

`symlinktest`（0.8 秒）：

```text
Start: test symlinks
test symlinks: ok
Start: test concurrent symlinks
test concurrent symlinks: ok
```

`bigfile`（139.8 秒）：

```text
wrote 65803 blocks
bigfile done; ok
```

完整 `usertests`（156.1 秒）：

```text
test writebig: OK
...
test bigdir: OK
ALL TESTS PASSED
```

这些结果覆盖 `grade-lab-fs` 的 bigfile 40 分、两项 symlinktest 各 20 分、usertests 19 分和 time 1 分，对应 100/100。

## 心得与答辩准备

双重间接块本质上是把文件逻辑块号拆成两级索引；符号链接则把“目录项定位到 inode”和“open 是否继续解释 inode 内容”为两个阶段。答辩时应能画出 13 个地址槽的布局，解释 65,803 的计算、`outer/inner` 索引、`log_write` 的必要性、`itrunc` 为什么必须逐层释放，以及 symlink 循环中 inode 锁和引用如何在每一跳交接。
