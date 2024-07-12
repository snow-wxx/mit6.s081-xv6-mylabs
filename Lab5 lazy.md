## Lecture 5 Page Faults

当试图访问PTE_V为0的虚拟地址或user访问PTE_U为0/kernel访问PTE_U为1以及其他违反PTE_W/PTE_R等flag的情况下会出现page faults。Page faults是一个exception，总共有3种page faults：

* load page faults：当load instruction无法翻译虚拟地址时发生
* store page faults：当store instruction无法翻译虚拟地址时发生
* instruction page faults：当一个instruction的地址无法翻译时发生

page faults种类的代码存放在`scause`寄存器中，无法翻译的地址存放在`stval`寄存器中。

在xv6中对于exception一律都会将这个进程kill掉，但是实际上可以结合page faults实现一些功能：

1. 可以实现写时复制 ***copy-on-write fork***。在`fork`时，一般都是将父进程的所有user memory复制到子进程中，但是`fork`之后一般会直接进行`exec`，这就会导致复制过来的user memory又被放弃掉。因此改进的思路是：子进程和父进程共享一个物理内存，但是mapping时将PTE_W置零，只有当子进程或者父进程的其中一个进程需要向这个地址写入时产生page fault，此时才会进行copy
2. 可以实现懒分配 ***lazy allocation***。旧的`sbrk()`申请分配内存，但是申请的这些内存进程很可能不会全部用到，因此改进方案为：当进程调用`sbrk()`时，将修改`p->sz`，但是并不实际分配内存，并且将PTE_V置0。当在试图访问这些新的地址时发生page fault再进行物理内存的分配
3. ***paging from disk***：当内存没有足够的物理空间时，可以先将数据存储在其他的存储介质（比如硬盘）上，将该地址的PTE设置为invalid，使其成为一个evicted page。当需要读或者写这个PTE时，产生Page fault，然后在内存上分配一个物理地址，将这个硬盘上的evicted  page的内容写入到该内存上，设置PTE为valid并且引用到这个内存物理地址

-------------------------------

### Lab 5：lazy allocation

* 在`sbrk()`时只增长进程的`myproc()->sz`而不实际分配内存。在kernel/trap.c中修改从而在产生page fault时分配物理内存给相应的虚拟地址。相应的虚拟地址可以通过`r_stval()`获得。`r_scause()`为13或15表明trap的原因是page fault

  注意：lazy allocation可能会造成`myproc()-sz`以下的内容没有被分配的情况，因此在unmap的过程中可能会出现panic，这是正常情况，需要`continue`

* 处理上述问题

  * `sbrk()`的参数为负的情况，deallocate即可
  * `fork()`中将父进程的内存复制给子进程的过程中用到了`uvmcopy`，`uvmcopy`原本在发现缺失相应的PTE等情况下会panic，这里也要`continue`掉
  * 当造成的page fault在进程的user stack以下（栈底）或者在`p->sz`以上（堆顶）时，kill这个进程。
  * 在`exec`中，`loadseg`调用了`walkaddr`，可能会找不到相应虚拟地址的PTE，此时需要分配物理地址
