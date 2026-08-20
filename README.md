# xv6 Labs 课程设计（2026）

本仓库用于完成同济大学 2026 年暑期操作系统课程设计的 xv6 方向。实验基于 MIT 6.S081 2021 的 RISC-V 版本，按实验分别维护分支，主分支保存项目导航、实验报告和答辩材料。

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

## 课程提交要求摘要

- 建议完成全部实验；若只完成部分实验，课程难度等级会相应下降。
- 实验报告须包含环境搭建、每个实验的目的/要求、步骤或过程、问题与解决方法、心得、相关代码和结果分析。
- 报告中给出源码托管链接，不提交源码压缩包；运行截图不宜过多。
- 答辩必须在线启动实验环境并现场运行结果。
- 线上答辩最晚截至 2026-08-31；答辩材料至少提前 24 小时发送给老师，越早完成可能有加分。

详细执行安排见 [项目路线图](docs/00-项目路线图.md)，现场运行见 [一天验收与答辩速查](docs/01-一天验收与答辩速查.md)。
