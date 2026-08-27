# Lab 8：锁与并行度

## 实验目的

1. 使用锁统计数据定位多核内核中的热点争用；
2. 把单一物理页空闲链表改造成 per-CPU 分配器，并在本地链表耗尽时安全偷页；
3. 把全局块缓存锁改造成散列桶锁，在保持“同一磁盘块最多一份缓存”不变量的同时提高并行度；
4. 通过压力测试、全内存回收和完整 `usertests` 同时验证性能与正确性。

## 实验环境

- 代码基线：MIT 6.S081 2021 `lock` 分支；
- 主机系统：Windows 11，Linux 环境为 WSL2 + Ubuntu 22.04 LTS；
- 交叉编译器：RISC-V GCC，目标为 64 位 RISC-V；
- 虚拟机：QEMU `virt` 机器、3 个 hart、128 MiB 内存。

所有编译、运行和测试命令均在 Ubuntu 终端中执行。

MIT 为本实验在 `acquire()` 中加入了统计：`#acquire()` 是获取次数，`#test-and-set` 是原子交换失败并继续自旋的次数。后者越大，说明多个 CPU 越频繁地争用同一把锁。

## Per-CPU 物理页分配器

原实现只有一个 `kmem.freelist` 和一把全局锁。不同 CPU 即使申请完全不同的页，也必须串行访问同一链表。

改造后 `kmem[NCPU]` 的每个元素包含独立的 spinlock 和 freelist：

- `kfree()` 在关闭中断后读取 `cpuid()`，把页归还当前 CPU 的链表；
- `kalloc()` 优先只访问当前 CPU 的链表；
- 本地链表为空时，从其他 CPU 链表一次最多拆下 1024 页，把一页立即返回，其余页接到本地链表。

读取 `cpuid()` 前必须 `push_off()`，直到本次分配结束后才 `pop_off()`，避免中断期间 CPU 身份失效。偷页时不持有自己的 freelist 锁，只持有一个 donor 锁；因此两个空链表的 CPU 不会各持一把锁再互等对方，消除了 ABBA 死锁。

批量偷页是慢路径频率与临界区长度的折中。初版每次只偷 64 页，正确性和争用测试都通过，但 `kalloctest` 的 50 轮全内存遍历超过时间限制。改为有上限的 1024 页后，仍不长期垄断整个 donor 链表，完整测试在 182.6 秒内结束。

## 散列块缓存

原 `bcache.lock` 同时保护缓存查找、块身份、引用计数和全局 LRU 链表，任何文件系统访问都会集中到同一临界区。

新设计包含：

- 31 个素数数量的 hash bucket，每桶一把 `bcache.bucket` 锁；
- `blockno % 31` 的稳定散列；
- 一把 `bcache.evict` 锁，只串行化 cache miss 和块身份变更；
- 每个 buffer 的 `lastuse` 逻辑时间，用于选择最久未使用且 `refcnt == 0` 的 victim。

命中路径只获取目标桶锁，增加引用计数后立刻释放，因此不同桶的读取可以并行。`brelse()`、`bpin()` 和 `bunpin()` 也只访问 buffer 当前所在的桶；最后一个引用释放时用原子逻辑时钟更新 `lastuse`。

## Miss、淘汰与单副本不变量

cache miss 的正确流程是：

1. 在目标桶中快速查找；
2. miss 后获取 `evictlock`，再查一次目标桶，处理另一 CPU 已经插入该块的竞态；
3. 使用原子只读函数扫描固定的 buffer 数组，找 `refcnt == 0` 且时间最旧的候选；
4. 按桶编号从小到大获取“候选旧桶”和“目标桶”，避免锁顺序死锁；
5. 在旧桶锁内重新检查候选引用计数。若快速命中路径刚刚占用了它，就释放桶锁并重新选择；
6. 从旧桶摘下候选，更新 `dev/blockno/valid/refcnt`，插入目标桶，再释放两把桶锁和 `evictlock`。

所有可能改变块身份的线程都必须持有 `evictlock`，而同一目标块的查找和插入在目标桶锁内复查，因此不可能同时生成两个相同 `(dev, blockno)` 的缓存副本。

扫描阶段不能获取全部桶锁。初版为了简单正确而依次锁住所有桶，功能测试能完成，但 miss 会阻塞所有无关命中，31 桶时争用反而达到 3385。最终实现只无锁读取候选元数据，并在真正移动时锁两个相关桶，争用降为 0。

## 实现检查与边界处理

实现过程中重点检查并处理了以下细节：

1. 偷页从不同时持有本地锁和 donor 锁，明确消除双向偷页死锁；
2. 所有 CPU 锁均在初始化阶段创建，名称以 `kmem` 开头，满足统计器要求；
3. 块缓存使用全局 miss/eviction 锁维护单副本不变量，而不是仅依赖 buffer 的独立 `used` 标志；
4. 使用逻辑时间保留近似 LRU 淘汰，而不是找到任意空 buffer；
5. victim 选择后在旧桶锁内复查引用计数，关闭无锁扫描与快速命中之间的竞态窗口；
6. 两桶操作采用固定编号顺序，同桶时只获取一次，避免搬移时自锁与 ABBA 死锁；
7. 热路径从不获取全局淘汰锁，也不会获取无关桶锁。

## 遇到的问题与解决方法

### 1. 偷页批次过小导致压力测试超时

64 页批次下 `test1` 已显示争用 0，`sbrkmuch` 也通过，但 `test2` 在 200 秒时只输出四个进度点。页数始终稳定，说明不是丢页，而是跨 CPU 搬运次数过多。将批次提高到 1024 后输出五个进度点并在 182.6 秒完成。

### 2. 哈希函数与桶数

把设备号异或进散列后，测试数据出现不理想碰撞，争用总数为 2922；改为 `blockno % 13` 降到 861，但仍超过 500。单纯增加到 31 桶时，因 miss 路径仍锁全部桶，争用反而升至 3385。最终同时采用 31 桶和“两桶淘汰”，才把总数降为 0。

### 3. 完整压力测试运行时间较长

锁实验包含多轮全内存遍历和并发压力测试，完整运行时间明显长于其他实验。调试时先分别运行 `kalloctest`、`bcachetest` 和 `usertests` 定位问题，确认各项通过后再执行 `make grade` 完成整体验证，避免把长时间运行误判为死锁。

### 4. `usertrap` 输出不是失败

完整 `usertests` 的 `MAXVAplus`、`kernmem`、`stacktest` 等用例会故意访问非法地址，内核打印 expected page fault 后杀死子进程。只要对应测试随后为 `OK` 且最终输出 `ALL TESTS PASSED`，这些 trap 正是保护机制生效的证据。

## 实验结果

`kalloctest`：

```text
tot= 0
test1 OK
start test2
total free number of pages: 32490 (out of 32768)
.....
test2 OK
```

运行时间 182.6 秒；8 把 `kmem` 锁的 `#test-and-set` 均为 0。完整回归中 `test sbrkmuch: OK`。

`bcachetest`：

```text
tot= 0
test0: OK
start test1
test1 OK
```

运行时间 21.0 秒；`bcache.evict` 和 31 把 `bcache.bucket` 锁的 `#test-and-set` 均为 0。

完整 `usertests` 在 279.2 秒内结束：

```text
test sbrkmuch: OK
...
test bigdir: OK
ALL TESTS PASSED
```

以上输出覆盖 `grade-lab-lock` 的全部评分点：kalloctest test1/test2、sbrkmuch、bcachetest test0/test1、完整 usertests 和 time 文件，对应 70/70。

## 实验心得

减少锁争用不是简单地“多加几把锁”，而是让锁的粒度与数据分区一致，并重新定义受保护的不变量。per-CPU freelist 降低了分配器热锁争用，散列桶锁提高了缓存命中路径的并行度；固定锁顺序、miss 后二次查找和 victim 引用计数复查则共同保证了正确性。
