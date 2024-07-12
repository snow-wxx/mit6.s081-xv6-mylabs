## Lecture 6 Interrupts & lock

进程在内核态中执行*top half*，中断时间中执行*bottom half*。top half是通过`read`或`write`这样的system call来进行调用的，从而能让这个设备执行I/O操作。当设备执行完I/O操作之后，将产生一个设备中断，这个设备驱动的interrupt  handler作为bottom half执行相应的操作。interrupt handler中没有任何用户进程的上下文，因此无法进行`copyin`或`copyout`，只有top half才能和用户进程进行交互。

- `PLIC`: Platform-Level Interrupt Controller，负责对从外部设备产生的中断进行管理
- `CLINT`: Core-Local Interrupter，负责定时器相关的中断

### Console input

console driver是一个设备驱动，通过UART串口接受输入的符号。用户进程通过`read` system call来从console中一行行读取输入

xv6中使用的UART是QEMU模拟的16550芯片。UART硬件对于进程来说是一组*memory-mapped*寄存器，即RISC-V上有一些物理地址是直接和UART设备相连的。UART的地址从0x10000000或`UART0`开始，每个UART控制寄存器的大小为1字节，其位置定义在kernel/uart.c中。

- `LSR`寄存器：line status register，用来指示输入的字节是否准备好被用户进程读取
- `RHR`寄存器：receive holding register，用来放置可以被用户进程读取的字节。当RHR中的一个字节被读取时，UART硬件将其从内部的FIFO硬盘中删除，当FIFO中为空时，`LSR`寄存器被置0
- `THR`寄存器：transmit holding register，当用户进程向`THR`写入一个字节时，UART将传输这个字节

xv6的`main`函数将调用`consoleinit`来初始化UART硬件，使得UART硬件在接收到字节或传输完成一个字节时发出中断

xv6 shell程序通过`user/init.c`开启的文件描述符来从console读取字节（在while循环中调用`getcmd`，在其中调用`gets`，再调用`read`system call）。在kernel中调用`consoleread`，等待输入完毕之后的中断，然后将输入缓存在`cons.buf`中，将输入`either_copyout`到user space后返回用户进程。如果用户没有输入完整的一行，则读取进程将在`sleep`system call中等待。

当用户输入了一个字符后，UART硬件将产生一个中断，这个终端将触发xv6进入trap。trap handler将调用`devintr`来通过`scause`寄存器判断是外部设备触发了这个中断，然后硬件将调用PLIC来判断是哪个外部设备触发的这个中断，如果是UART触发的，`devintr`将调用`uartintr`。`uartintr`将读取从UART硬件中写入的字符然后将其传送给`consoleintr`，`consoleintr`将积累这些字符直到整行都已经被读取，然后将唤醒仍在`sleep`的`consoleread`。当`consoleread`被唤醒后，将这一行命令复制给user space然后返回。

**RISC-V对中断的支持**：

* `SIE`(supervisor interrupt enable) 寄存器用来控制中断，其中有一位是控制外部设备的中断（SEIE），一位控制suffer interrupt(一个CPU向另外一个CPU发出中断)(SSIE)，一位控制定时器中断(STIE)
* `SSTATUS`(supervisor status)寄存器，对某一个特定的CPU核控制是否接收寄存器，在intr_on`被设置`
* `SIP`(supervisor interrupt pending)寄存器，可以观察这个寄存器来判断有哪些中断在pending

**case study**:

用户在键盘上输入了一个字符l，这个l通过键盘被发送到UART，然后通过PLIC发送到CPU的一个核，这个核产生中断，跑到`devintr`，`devintr`发现是来自UART的，调用`uartintr`，调用`uartgetc()`通过`RHR`寄存器来获取这个字符，然后调用`consoleintr`，判断这个字符是否是特殊字符(backspace等)，如果不是则将这个字符通过`consputc(c)`echo回给user，然后将其存储在`cons.buf`中，当发现整行已经输入完成后(`c=='\n' || c ==C('D'))`)，唤醒`consoleread()`

----------------------------

### Console output

对console上的文件描述符进行`write`system call，最终到达`uartputc`函数。输出的字节将缓存在`uart_tx_buf`中，这样写入进程就不需要等待UART硬件完成字节的发送，只要当这个缓存区满了的情况下`uartputc`才会等待。当UART完成了一个字符的发送之后，将产生一个中断，`uartintr`将调用`uartstart`来判断设备是否确实已经完成发送，然后将下一个需要发送的字符发送给UART。因此让UART传送多个字符时，第一个字符由`uartputc`对`uartstart`的调用传送，后面的字符由`uartintr`对`uartstart`的调用进行传送

*I/O concurrency*：设备缓冲和中断的解耦，从而让设备能够在没有进程等待读入的时候也能让console driver处理输入，等后面有进程需要读入的时候可以不需要等待。同时进程也可以不需要等待设备而直接写入字符到缓冲区。

在`consoleread`和`consoleintr`中调用了`acquire`来获取一个锁，从而保护当前的console driver，防止同时期其他进程对其的访问造成的干扰。

------------------------

### Timer interrupts

xv6用计时器中断来在线程间切换，`usertrap`和`kerneltrap`中的`yield`也会导致这种进程切换。RISC-V要求定时器中断的handler放在machine mode而不是supervisor mode中，而machine  mode下是没有paging的，同时有另外一套完全独立的控制寄存器，因此不能将计时器中断的handler放在trap机制中执行。

`kernel/start.c`（在`main`之前）运行在machine mode下，`timerinit()`在`start.c`中被调用，用来配置CLINT(*core-local interruptor*)从而能够在一定延迟之后发送一个中断，并设置一个类似于trapframe的scratch area来帮助定时器中断handler将寄存器和CLINT寄存器的地址保存到里面，最终`start`设置`timervec`到`mtvec`(*machine-mode trap handler*)中使得在machine mode下发生中断后跳转到`timervec`然后enable定时器中断。

由于**定时器中断可能在任意时间点发生**，包括kernel在执行关键的操作时，无法强制关闭定时器中断，因此定时器中断的发生不能够影响这些被中断的操作。解决这个问题的方法是定时器中断handler让RISC-V CPU产生一个"software interrupt"然后立即返回，software  interrupt以正常的trap机制被送到kernel中，可以被kernel禁用。

`timervec`是一组汇编指令，将一些寄存器保存在scratch area中，告知CLINT产生下一次定时器中断的时间，让RISC-V产生一个software interrupt，恢复寄存器并返回到trap.c中，判断`which_dev==2`为定时器中断后调用`yield()

-----------------

计时器中断将会通过调用`yield`进行强制的线程切换从而使CPU能够在各个内核线程之间均匀分配时间。

UART是通过对UART控制寄存器一个字节一个字节读取来获取数据的，这种方式叫做*programmed I/O*，因为是软件控制了数据的I/O，缺点是速度比较慢。DMA(*Directed Memory Access*)直接向RAM写入和读取数据，速度很快。现代的硬盘和网卡驱动使用DMA。

由于中断非常耗时，因此可以用一些技巧来减少中断:

1. 用一个中断来处理很多一段时间内的事件。
2. 彻底禁止设备的中断，让CPU定时去检查这些设备是否有任务需要处理，这种技巧叫做*polling*

---------------------

### Race conditions

锁提供了一种互斥机制，一段时间内只有一个CPU才能拥有这个锁，如果一个锁和一个被共享的数据结构联系起来，那么这个数据结构一次只能被一个CPU使用

*race condition*：一个内存地址同时被至少一个写入操作访问，会造成bug

当两个进程同时要求一个相同的锁时，这两个进程发生冲突，xv6对进程锁冲突没有做预防举措

### lock

xv6有两种锁：spinlock和sleep-lock。

如果一块代码需要同时拥有多个锁，那么应该让其他需要相同锁的进程按照相同的顺序acquire这些锁，否则可能出现死锁，xv6的文件系统中有一个很长的lock chain，如果要创建一个文件需要同时拥有文件夹的锁、新文件的inode的锁、磁盘块缓冲区的锁、磁盘驱动器的`vdisk_lock`的锁以及调用进程的`p->lock`的锁

除了lock ordering之外，锁和中断的交互也可能造成死锁，对此，如果一个中断中需要获取某个特定的spinlock，那么当CPU获得了这个spinlock之后，该中断必须被禁用。xv6的机制则更加保守：当CPU获取了任意一个lock之后，将disable掉这个CPU上的所有中断（其他CPU的中断保持原样）。当CPU不再拥有spinlock时，将通过`pop_off`重新使能中断

**spinlock的两个缺点**：

1. 如果一个进程拥有一个锁很长时间，另外一个企图acquire的进程将一直等待。
2. 当一个进程拥有锁的时候，不允许把当前使用的CPU资源切换给其他线程，否则可能导致第二个线程也acquire这个线程，然后一直无法切回到原来的线程，无法release锁，从而导致死锁。

*sleep-locks*，可以在试图`acquire`一个被拥有的锁时`yield` CPU。spin-lock适合短时间的关键步骤，sleep-lock适合长时间的锁。

----------------------------------

### RCU

RCU(Read-Copy Update)是一种能让多个读进程对链表进行同时读取，并让一个写进程同时对链表进行写入修改操作的机制，这种机制避免了进程进行读/写操作都需要获取锁而造成的锁竞争问题，适用于大量进程同时对链表结构进行读取的操作。

基本原理是：写进程在写入某一个链表中的节点时，比如`head->E1->E2->E3->nil`试图修改`E2->content`，则不直接修改E2->content，因为在修改E2->content的过程中可能会有别的进程在读，此时可能读入写了一半的内容，我们希望一个读进程读取的内容要么是修改之前的，要么是修改之后的，而不是修改一半的内容。读进程的操作是

1. lock，防止其他写进程同时进行写入
2. e = alloc()，新分配一个element
3. e->next = E2->next，此时同时有2个element指向E3，但是其他读进程在读的时候还是读取的是旧的E2
4. e->content = new_content
5. E1->next = e，此时其他读进程在读的时候是新的E2，这是一个原子操作
6. unlock

由于编译器有时候为了优化会将2 3 4 5等步骤打乱，因此需要在第5步之前设置memory barrier，即只有在2 3 4均完成的情况下才能执行第5步

同时需要释放原先的E2，但是由于可能很多读进程已经获取了对原先E2的指针，必须等待这些读进程读取完毕不再使用E2才能将原先的E2释放掉，这是通过以下规则实现的：

1. 所有的读进程不能够在进行context switch时拥有着对RCU-protected data的指针，也就是说在读进程读完E2之前，不能yield CPU
2. 写进程需要等到所有的CPU都进行了一次context switch才能释放掉原先的数据，也就是E2(通过`synchronize_rcu()`实现)

-------------------------

### Lab 6: copy on write fork

* 修改`uvmcopy()`，使得父进程在调用该函数时将父进程的物理页映射到子进程的页表，而不是直接给子进程的页表分配新的物理页。要设置`PTE_COW`(`1L >> 8`)来表明这是一个copy-on-write页，在陷入page fault时需要进行特殊处理。将`PTE_W`置零，将该物理页的`refc`设置为1.

* 在`usertrap()`中用`scause() == 13 || scause() == 15`来判断是否为page fault，当发现是page fault并且`r_stval()`的物理页是COW页时，说明需要分配物理页，并重新映射到这个页表相应的虚拟地址上，当无法分配时，需要kill这个进程。注意：需要判断虚拟地址是否是有效的，其中包括需要判断这个虚拟地址是不是处在stack的guard page上，通过`va <= PGROUNDDOWN(p->trapframe->sp) && va >= PGROUNDDOWN(p->trapframe->sp) - PGSIZE`进行判断

  在将新的物理地址映射给页表之前，需要注意设置PTE_W为1，PTE_COW为0，设置完成之后尝试`kfree`掉旧的物理页，从而保证当没有任何进程的页表引用这个物理页的情况下这个物理页被释放掉。

* 为了记录每个物理页被多少进程的页表引用，需要在`kalloc.c`中定义一个结构体`refc`，其中有一个大小为`PGROUNDUP(PHYSTOP)/PGSIZE`的int array来存放每个物理页的引用数。由于这个结构体是被所有的进程共享的，因此需要用一个spinlock进行保护, 在进行`kalloc`时，将对该物理页的引用加一，相应的，在进行`kfree`时，要对该物理页的引用减一，然后再判断对该物理页的引用是否已经为0，如果已经为0，则将该物理页push回`freelist`

  注意，一开始进行`kinit`的时候调用了`freerange(end, (void*)PHYSTOP)`，里面对所有物理页都进行了一次`kfree`，由于之前没有进行过`kalloc`，所以会导致每一页的初始`refc.count`都变成-1，因此需要在`kinit`时再给每一个物理页的`refc.count`加1。

*  由于`copyout`函数直接将kernel中的物理地址的内容复制给了用户进程中的物理地址, 没有经过mmu, 也无法进入page fault, 因此当将内容复制到用户进程的虚拟地址时，会将原来的内容覆盖掉而不是进行cow，因此需要修改`copyout`，调用`cow_alloc`
