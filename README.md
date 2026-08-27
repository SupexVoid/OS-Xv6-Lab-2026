# xv6 Labs 课程设计（2026）

本项目基于 MIT 6.S081 2021 的 RISC-V 版本 xv6 教学操作系统，完成 util、syscall、pgtbl、traps、cow、thread、net、lock、fs 和 mmap 十个实验。各实验使用独立分支保存，便于分别编译、测试和查看实现。

## 实验环境

- Windows 11
- WSL2
- Ubuntu 22.04 LTS
- RISC-V 交叉编译工具链
- GNU Make、GDB、QEMU

所有 xv6 编译、测试和运行命令均在 Ubuntu 终端中执行。

## 实验分支

| 序号 | 分支 | 实验内容 |
| --- | --- | --- |
| 1 | `util` | Unix utilities |
| 2 | `syscall` | System calls |
| 3 | `pgtbl` | Page tables |
| 4 | `traps` | Traps |
| 5 | `cow` | Copy-on-write fork |
| 6 | `thread` | Multithreading |
| 7 | `net` | Network driver |
| 8 | `lock` | Parallelism and locking |
| 9 | `fs` | File system |
| 10 | `mmap` | Memory-mapped files |

## 编译与运行

切换到需要查看的实验分支：

```bash
git switch <branch>
```

编译并启动 xv6：

```bash
make clean
make qemu
```

运行对应实验的完整测试：

```bash
make grade
```

退出 QEMU：先按 `Ctrl+A`，再按 `X`。

## 参考资料

- [MIT 6.S081: Operating System Engineering](https://pdos.csail.mit.edu/6.S081/2021/)
- [xv6: a simple, Unix-like teaching operating system](https://pdos.csail.mit.edu/6.S081/2021/xv6/book-riscv-rev2.pdf)
