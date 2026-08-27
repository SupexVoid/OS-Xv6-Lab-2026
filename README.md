# xv6 Labs 课程设计（2026）

本仓库用于完成同济大学 2026 年暑期操作系统课程设计的 xv6 方向。实验基于 MIT 6.S081 2021 的 RISC-V 版本，按实验分别维护分支，主分支保存项目导航、实验报告和答辩材料。

## 实验环境

项目沿用 MIT 课程和参考项目的常规 Linux 路线：Windows 11 上启用 WSL2，在 Ubuntu 22.04 LTS 中安装 RISC-V 交叉编译工具链、GNU Make、GDB 和 QEMU。所有编译、测试和现场演示命令均在 Ubuntu 终端执行。

## 参考与边界

- 官方实验起点：MIT `xv6-labs-2021` 各 Lab 分支。
- 学习与复现参考：Iuriak/OS-Xv6-Lab-2023 的实验分支、流程与报告结构。
- 每个分支保留自己的提交和测试记录；答辩前应能说明关键机制与演示命令。

## 分支规划

| 顺序 | 本仓库分支 | MIT Lab | 内容 |
| --- | --- | --- | --- |
| 1 | `util` | util | Unix utilities |
| 2 | `syscall` | syscall | System calls |
| 3 | `pgtbl` | pgtbl | Page tables |
| 4 | `traps` | traps | Traps |
| 5 | `cow` | cow | Copy-on-write fork |
| 6 | `thread` | thread | Multithreading |
| 7 | `net` | net | Network driver |
| 8 | `lock` | lock | Parallelism/locking |
| 9 | `fs` | fs | File system |
| 10 | `mmap` | mmap | Memory-mapped files |

每个分支的验收命令统一为：

```bash
make grade
```

现场答辩时还需要在对应分支运行：

```bash
make qemu
```

退出 QEMU：先按 `Ctrl-a`，再按 `x`。
