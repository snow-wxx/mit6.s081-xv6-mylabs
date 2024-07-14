## Lecture 10 Virtual memory for user applications

### mmap

将一个文件或者其他对象映射到进程的地址空间，实现文件磁盘地址和进程虚拟地址空间中一段虚拟地址的一一对应关系

**为什么要建立文件磁盘地址和进程虚拟地址空间的映射**？因为常规的文件系统操作是用户态发起`read`syscall，然后在buffer cache中查找是否有相应的数据，如果没有就从磁盘中拷贝数据到buffer cache中，因为buffer cache处在内核态，因此需要将buffer cache`copyout`到用户进程的虚拟内存中，这就需要2次拷贝操作，而在mmap中只需要直接将文件数据拷贝到对应的用户空间虚拟内存即可。

**文件映射**：使用文件内容初始化物理内存

`addr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, offset)` NULL表示让kernel自己查找需要映射到的文件磁盘地址

**匿名映射**：不需要打开文件，初始化一个全为0的内存空间

私有匿名映射用于`malloc`给进程分配虚拟的地址空间，当各个进程读的时候共享物理内存，写的时候copy on write

```
mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
```

共享匿名映射在进行fork时不会采用写时copy on write，父子进程完全共享同样的物理内存页，实现父子进程的IPC

-----------------

### Virtual memory implementation

**VMA**：VMA (Virtual Memory Area)由一连串的虚拟地址组成，每个VMA都有相同的权限，指向相同的对象（文件、匿名内存等）

**User-level traps：**比如，当PTE被标记为invalid时发生缺页异常，CPU从用户态陷入内核态

内核态保存trapframe

内核将查看产生错误的VMA，判断接下来的动作。比如一个用户进程设置了一个sig_action(handler for  signal)，则内核将产生一个signal，将这个signal传播给用户态，回到用户态调用sig_action，然后sig_action将调用mprotect来修改PTE

最后sig_action将返回kernel，kernel继续返回到被中断的进程。

--------------

### Garbage collector

为了将程序分配的内存在程序退出之后能够自动清理掉，需要垃圾收集器，其作用是从一个根集合root出发，递归地寻找所有被引用的对象，剩下的所有没有被引用到的对象就是垃圾内存，需要被清除。

**复制算法**是将Heap内存空间分为2块区域，一块是from区域一块是to区域，将from区域中的所有能够被root查找到的对象全部复制到to区域中，然后将from区域的所有内存清除掉。这个算法的缺点在于gc线程运行的过程中需要暂停所有的用户线程。

**Baker实时垃圾收集算法**：to区域包括new区域、scanned区域和unscanned区域。在最开始先将root复制到to区域，当用户线程调用`new`来分配新的对象时，from区域中被root及其它对象指向的对象会被增量地复制到to区域中。几个规则：

- 用户线程只能看到to-space的指针，也就是说每个用户线程取得的对象指向的对象只能在to-space中，如果检查发现在from-space中，就要将这个被指向的对象拷贝到to-space，并且更新指针，然后才能返回给用户线程。
- 每次调用new，被分配的对象放在new区域中，并且new区域中所有的对象的指针都只能指向to区域
- scanned区域中的对象只能拥有指向to区域的指针，unscanned区域中的对象可以拥有指向to或from区域的指针。

为了避免检查每个从内存中取回的对象的指针使其满足第一个规则，gc给每个unscanned区域中的对象都加了no  access，使得每次试图访问这个unscanned对象时都会陷入缺页保护异常，然后gc扫描这个对象，将其指向的对象拷贝到to区域中，然后再恢复访问权限。这个机制也提供了concurrency，因为可以防止gc thread和用户线程同时修改unscanned page，因为unscanned page已经被加上了none  access保护，用户线程是无法访问的。但是由于**map2**的存在，可以将相同的物理地址再映射一个给gc thread，其访问权限是R|W，因此用户线程不能访问的unscanned page gc线程是可以访问的
