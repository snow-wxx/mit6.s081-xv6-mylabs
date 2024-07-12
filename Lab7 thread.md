## Lecture 7 Scheduling

由于操作系统需要同时运行的进程的数量可能大于电脑CPU的数量，因此需要一种让进程time share CPU的机制，理想情况下这种机制对于用户进程应该是**透明**的，即让每个进程都认为自己拥有一个单独的虚拟CPU

线程：一个串行的指令执行

xv6的kernel thread支持共享内存，user process不支持

### Multiplexing

xv6在2种情况下在进程之间切换从而实现multiplexing：

1. `sleep`/`wakeup`机制：进程等待设备或I/O、等待子进程退出、在`sleep`sys call中等待 
2. 周期性强迫一个进程进行切换，防止一个进程占用过长时间。

![image-20210203212120616](https://fanxiao.tech/img/posts/MIT_6S081/image-20210203212120616.png)

### Context Switching

进程的上下文切换涉及到用户空间和内核空间之间的来回转换。当进程需要切换时，首先通过system call或中断陷入内核态，进入该进程的内核线程，然后将**内核线程**的上下文（注意不是用户进程的上下文，用户进程的上下文已经保存在了trapframe里面）切换到当前CPU的scheduler线程，再将上下文切换到需要运行的进程的内核线程，最后返回用户空间。

从一个内核线程切换到另一个线程需要保存旧线程的寄存器，恢复新线程之前保存的寄存器。`sp`和`pc`将在此过程中被保存和切换。`swtch`可以实现这种寄存器组状态(也叫上下文)的保存和切换。当进程需要`yield`CPU时，这个进程的内核线程将调用`swtch`来保存上下文并切换到scheduler的上下文，所有的上下文都保存在`struct context`中。`swtch`的传入参数为`struct context *old`和`struct context *new`

`yield()`函数切换了进程的状态为RUNNABLE，调用了`sched()`。`sched`调用了`swtch(&p->context, &mycpu()->context)`来将上下文切换到`cpu->scheduler

`swtch`只保存callee saved寄存器，caller saved寄存器在栈中被调用的代码保存。`swtch`并没有保存`pc`寄存器，而是保存了`ra`，当恢复了新的进程之前保存的`ra`寄存器后，将返回到`ra`寄存器指向的上一个进程调用`swtch`的代码。如果保存`pc`寄存器，将只能回到`swtch`本身。由于切换到的`&mycpu()->context`是被`scheduler`对`swtch`的调用所保存的，因此当进行`swtch`时，我们将返回到`scheduler`，栈指针也将指向当前CPU的scheduler stack。

------------------

### Scheduling

调度器(scheduler)是每个CPU中都会运行的一个特殊的线程，这个线程中不断运行`scheduler`函数，来选取下一个需要运行的进程。

想要`yield`CPU的进程首先要获取自己进程的锁`p->lock`（防止其他CPU获取这个进程），修改当前的状态到`RUNNABLE`，`release`掉自己获取的其他锁，加载`cpu->scheduler`的上下文，返回到`scheduler()`之后，`release`掉自己的进程锁。

在`scheduler`调用`swtch`到新的进程之前，`scheduler`需要已经获取这个进程的锁，并且将对这个进程的锁传递给被切换到的这个新的进程中，让新进程来`release`这个锁。一般来说，一个锁应该由`acquire`它的进程来进行`release`，但是由于一个进程的`p->state`是在`scheduler`中被改变的，需要对其进行保护，因此需要在`scheduler`中就获取这个进程的锁

当一个新的进程是第一次被`scheduler`调度的时候，不返回到`sched`，而是返回到`forkret`（因为之前并没有从`sched`中调用过`swtch`）。`forkret`将`p->lock`释放掉，然后回到`usertrapret`。

--------------------

### mycpu & myproc

xv6为每一个CPU都有一个`struct cpu`，记录当前运行在这个CPU上的进程的指针`struct proc *proc`、保存的寄存器`struct context context`、`push_off`的nesting的数量`int noff`等变量。

RISC-V将所有CPU进行编号，该编号称为*hartid*，确保每个CPU的hartid都保存在这个CPU的`tp`寄存器内，可以让`mycpu`通过这个hartid来索引到一个`struct cpu`数组`cpus[]`中，从而获取对当前CPU的`struct cpu`的引用。当获取`struct cpu`之后如果发生了中断导致CPU被切换了，那么获取的`struct cpu`将是不正确的，因此需要用`push_off`来保证当前的中断使能被关闭。

使用`myproc()`函数来返回一个指向当前CPU运行的进程`c->proc`的指针。

----------------------

Lab 7: thread

* 实现一个用户层面的线程系统,
  * 在`uthread_switch.S`中,仿照`swtch.S`进行上下文寄存器的保存和切换
  * `thread_create()`中,第一次创建进程时需要初始化`ra`和`sp`寄存器. `ra`寄存器需要存放传入的函数地址, `sp`寄存器传入当前线程的栈底(最开始的位置)
  * `thread_schedule()`中,直接调用`thread_switch`,仿照`swtch`的格式进行上下文切换
* 对`notxv6/ph.c`进行补充,以实现多线程情况下能够正确地向一个哈希表插入键值对,其核心思想就是给每个线程对哈希表的操作都加锁.
  * 定义一个全局变量`pthread_mutex_t lock[NBUCKET];` 其中`NBUCKET`是同时可以进行操作的线程最大数量, 并对这个锁数列进行初始化
  * 在`put()`和`get()`函数中上锁和解锁以保护对哈希表键值对的读写操作.
* 实现线程的同步,即每一个线程必须等待其他所有线程都到达barrier之后才能继续进行下面的操作,需要用到`pthread_cond_wait(&cond, &mutex)`和`pthread_cond_broadcast(&cond)`来进行线程的sleep和唤醒其他所有`&cond`中睡眠的线程. 需要对`barrier.c`中的`barrier()`进行实现.
  * 当每个线程调用了`barrier()`之后,需要增加`bstate.nthread`以表明到达当前round的线程数量增加了1, 但是由于`bstate`这个数据结构是线程之间共享的, 因此需要用`pthread_mutex_lock`对这个数据结构进行保护. 当`bstate.nthread`的数量达到线程总数`nthread`之后, 将`bstate.round`加1. 注意, 一定要等到所有的线程都达到了这个round, 将`bstate.nthread`清零之后才能将所有正在睡眠的线程唤醒, 否则如果先唤醒线程的话其他线程如果跑得很快, 在之前的线程将`bstate.nthread`清零之前就调用了`bstate.nthread++`,会出现问题(round之间是共用`bstate.nthread`的)
