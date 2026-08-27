# Lab 10：mmap

## 实验目的

为 xv6 增加文件映射系统调用 `mmap` 和 `munmap`。映射采用惰性分配：建立映射时只记录虚拟内存区域，首次访问页面时由缺页异常分配物理页并从文件读取内容；共享映射解除时把修改写回文件。

## 实验环境

- 代码基线：MIT 6.S081 2021 `mmap` 分支；
- 主机系统：Windows 11，Linux 环境为 WSL2 + Ubuntu 22.04 LTS；
- 工具链：RISC-V 交叉编译器、GNU Make、GDB 和 QEMU。

所有编译、运行和测试命令均在 Ubuntu 终端中执行。

## 实现方法

本分支以 MIT 6.S081 2021 的 `mmap` starter 为起点，按照实验要求完成以下改动：

- 注册 `mmap`、`munmap` 两个系统调用；
- 在进程结构中保存 VMA 数组；
- 在用户缺页异常中加载对应文件页；
- 在 `fork` 时复制 VMA，并增加文件引用计数；
- 在 `munmap` 和进程退出时解除映射并处理共享页写回；
- 调整页表复制、解除映射逻辑，使惰性映射中尚未分配的页面可以被跳过。

## 遇到的问题与解决方法

### 1. 惰性页面尚未建立 PTE

建立 VMA 时不会立即分配物理页，因此 `fork`、`munmap` 和进程退出可能遇到尚未访问的虚拟页。原有页表复制与解除映射代码把缺少 PTE 视为内核错误。解决方法是先检查映射是否存在，只复制或解除已经实际分配的页面，对尚未触发缺页的范围直接跳过。

### 2. 共享映射写回范围计算

部分解除映射时，虚拟地址、VMA 起始地址和文件偏移之间容易出现偏移错误。实现中使用 `file_offset + (unmap_addr - vma_start)` 计算写回位置，并按页逐段处理；私有映射不写回，共享且可写的映射才调用文件写入路径。

### 3. VMA 与文件引用的生命周期

VMA 保存文件指针后必须增加文件引用；`fork` 复制 VMA 时也要增加引用，完全解除映射或进程退出时再对应调用 `fileclose()`。通过对称维护引用计数，避免文件被提前关闭或进程结束后仍残留引用。

## 验证结果

在 QEMU 的 xv6 shell 中运行：

```text
$ mmaptest
mmap_test: ALL OK
fork_test OK
mmaptest: all tests succeeded
```

随后运行完整回归：

```text
$ usertests
ALL TESTS PASSED
```

## 运行方法

```bash
git switch mmap
make qemu
```

进入 xv6 后运行：

```text
mmaptest
```

看到 `mmaptest: all tests succeeded` 表明测试通过。退出 QEMU：先按 `Ctrl+A`，再按 `X`。

## 实验心得

`mmap` 把文件、虚拟地址空间、缺页异常和进程生命周期联系在一起。惰性加载减少了未访问页面的开销，但也要求缺页处理、`fork`、`munmap` 和进程退出共享一致的 VMA 与文件引用规则；共享映射的写回还必须严格计算文件偏移和解除映射范围。
