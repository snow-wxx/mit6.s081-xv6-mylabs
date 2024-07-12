## Lecture 3 Page Tables

页表让每个进程都拥有自己独立的虚拟内存地址，从而实现内存隔离。

### Paging Hardware

xv6运行于Sv39 RISC-V，即在64位地址中只有最下面的39位被使用作为虚拟地址，其中底12位是页内偏移，高27位是页表索引，即4096字节(212212)作为一个page，一个进程的虚拟内存可以有 227227个page，对应到页表中就是227227个page table entry (PTE)。每个PTE有一个44位的physical page number  (PPN)用来映射到物理地址上和10位flag，总共需要54位，也就是一个PTE需要8字节存储。即每个物理地址的高44位是页表中存储的PPN，低12位是页内偏移，一个物理地址总共由56位构成。

在实际中，页表并不是作为一个包含了227227个PTE的大列表存储在物理内存中的，而是采用了**三级树状**的形式进行存储，这样可以让页表分散存储。每个页表就是一页。**第一级页表是一个4096字节的页**，**包含了512个PTE**（因为每个PTE需要8字节），**每个PTE存储了下级页表的页物理地址**，**第二级列表由512个页构成，第三级列表由512*512个页构成**。因为每个进程虚拟地址的高27位用来确定PTE，对应到3级页表就是最高的9位确定一级页表PTE的位置，中间9位确定二级页表PTE的位置，最低9位确定三级页表PTE的位置。第一级根页表的物理页地址存储在`satp`寄存器中，每个CPU拥有自己独立的`satp`

PTE flag可以告诉硬件这些相应的虚拟地址怎样被使用，比如`PTE_V`表明这个PTE是否存在，`PTE_R`、`PTE_W`、`PTE_X`控制这个页是否允许被读取、写入和执行，`PTE_U`控制user mode是否有权访问这个页，如果`PTE_U`=0，则只有supervisor mode有权访问这个页。

---------------

### Kernel address space

每个进程有一个页表，用于描述进程的用户地址空间，还有一个内核地址空间（所有进程共享这一个描述内核地址空间的页表）。为了让**内核使用物理内存和硬件资源**，内核需要按照一定的规则排布内核地址空间，以能够确定哪个虚拟地址对应自己需要的硬件资源地址。用户地址空间不需要也不能够知道这个规则，因为**用户空间不允许直接访问这些硬件资源**。

在虚拟地址和物理地址中，kernel都位于`KERNBASE=0x80000000`的位置，这叫做直接映射。用户空间的地址分配在free memory中

有一些不是直接映射的内核虚拟地址：

- trampoline page（和user pagetable在同一个虚拟地址，以便在user space和kernel space之间跳转时切换进程仍然能够使用相同的映射，真实的物理地址位于kernel text中的`trampoline.S`）
- kernel stack page：每个进程有一个自己的内核栈kstack，每个kstack下面有一个没有被映射的guard page，guard  page的作用是防止kstack溢出影响其他kstack。当进程运行在内核态时使用内核栈，运行在用户态时使用用户栈。**注意**：还有一个内核线程，这个线程只运行在内核态，不会使用其他进程的kstack，内核线程没有独立的地址空间。

xv6对kernel space和PHYSTOP之间的物理空间在运行时进行分配，分配以页(4096 bytes)为单位。分配和释放是通过对空闲页链表进行追踪完成的，分配空间就是将一个页从链表中移除，释放空间就是将一页增加到链表中 

-----------------

### User space memory

每个进程有自己的用户空间下的虚拟地址，这些虚拟地址由每个进程自己的页表维护，用户空间下的虚拟地址从0到MAXVA，当进程向xv6索要更多用户内存时，xv6先用`kalloc`来分配物理页，然后向这个进程的页表增加指向这个新的物理页的PTE，同时设置这些PTE的flag

------------------------

### Lab 3: pgtbl

* 在程序刚刚启动(pid==1)时打印输出当前进程的pagetable
* 为每个进程添加一个kernel pagetable来取代之前的global page table
  * 在`struct proc`添加一个每个进程的kernel pagetable
  * 实现一个`proc_kpt_init()`函数来为每个进程初始化kernel page table，其中`uvmmap`类似于`kvmmap`，只不过`kvmmap`是直接对全局的`kernel_pagetable`进行`mappage`，而`uvmmap`并没有指定page table
  * 在`allocproc()`中调用初始化进程内核页表的函数，要让每个procees kernel pagetable拥有这个进程的kernel stack，因此在`allocproc`中分配kstack并将其map到`p->kernelpt`上
  * 修改`scheduler()`以将进程的kernel page table加载到`satp`寄存器中，如果CPU空闲，就使用global kernel page table
  * 释放掉process kernel page table，注意对于KERNBASE以下的部分，这个process kernel page  table只能unmap这些PTE，而不能真的释放这些物理资源，因为KERNBASE以下的部分其他进程是共享的，因此要先释放掉进程的kstack物理资源，然后用`freewalk_kproc`这个函数将pagetable unmap和释放的同时也保留叶节点指向的物理资源
* 将每个进程的user page table复制到进程kernel page table上，从而让每个进程在`copyin`的时候不需要再利用rocess user page table来翻译传入的参数指针，而可以直接利用process kernel page table来dereference
  * 先实现一个复制page table的函数`u2kvmcopy`来将user page table复制到process kernel page table，注意在复制的过程中需要清除原先PTE中的PTE_U标志位，否则kernel无法访问
  * 在每个改变了user page table的地方要调用`u2kvmcopy()`使得process kernel page table跟上这个变化。
  * 在`userinit`中复制process kernel page
  * 最后将`copyin()`和`copyinstr()`替换为`copyin_new()`和`copyinstr_new()`

-----------------------

## 
