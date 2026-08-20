# Lab 10：mmap

## 实验目的

为 xv6 增加文件映射系统调用 `mmap` 和 `munmap`。映射采用惰性分配：建立映射时只记录虚拟内存区域，首次访问页面时由缺页异常分配物理页并从文件读取内容；共享映射解除时把修改写回文件。

## 复现方式

本分支以 MIT 6.S081 2021 的 `mmap` 分支为起点，按学长仓库 `senior/mmap` 的 Lab 10 提交复现。主要改动包括：

- 注册 `mmap`、`munmap` 两个系统调用；
- 在进程结构中保存 VMA 数组；
- 在用户缺页异常中加载对应文件页；
- 在 `fork` 时复制 VMA，并增加文件引用计数；
- 在 `munmap` 和进程退出时解除映射并处理共享页写回；
- 调整页表复制、解除映射逻辑，使惰性映射中尚未分配的页面可以被跳过。

Windows 本地验证另外保留了 RISC-V 64 位编译参数和 `mkfs` 二进制文件模式适配；这些设置不改变 xv6 内核功能。

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

## 答辩演示

```bash
git switch mmap
make qemu
```

进入 xv6 后运行：

```text
mmaptest
```

看到 `mmaptest: all tests succeeded` 即完成本实验演示。退出 QEMU：先按 `Ctrl+A`，再按 `X`。
