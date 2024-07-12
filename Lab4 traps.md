## Lecture 4 traps

3种可能的情况使得CPU暂停对正常指令的执行：1. syscall，移交给kernel 2. exception，指令执行了非法操作 3. 设备中断。以上情况合并称为*trap*。

trap应该对于被打断的指令是透明的，也就是说被打断的指令不应该知道这个地方产生了trap，产生trap之后现场应该得以恢复并继续执行被打断的指令。

xv6对trap的处理分为四个阶段：1. RISC-V CPU的硬件的一些动作 2. 汇编文件为了kernel C文件进行的一些准备 3. 用C实现的trap handler 4.  system call / device-driver service routine

### RISC-V trap machinery

RISC-V CPU有一系列的控制寄存器可以通知kernel发生了trap，也可以由kernel写入来告诉CPU怎样处理trap

- `stvec`：trap handler的地址，由kernel写入
- `sepc`：保存trap发生时的现场program counter，因为接下来`pc`要被取代为`stvec`。`sret`是从trap回到现场的指令，将`sepc`写回到`pc`
- `scause`：一个trap产生的原因代码，由CPU写入
- `sscratch`：放在trap handler的最开始处
- `sstatus`：控制设备中断是否被开启，如果`sstatus`中的SIE位被清除，则RISC-V将推迟设备中断。SPP位指示这个trap是在user space中产生的还是在kernel space产生的，并将控制`sret`回到什么模式

以上寄存器只在supervisor模式下发生的trap被使用，当发生除了计时器中断以外的其他类型的trap时，RISC-V将执行以下步骤：

1. 如果trap是一个设备产生的中断，而SIE又被清除的情况下，不做下方的任何动作
2. 清除SIE来disable一切中断
3. 把`pc`复制到`sepc`
4. 把当前的模式(user / supervisor)保存到SPP
5. 设置`scause`寄存器来指示产生trap的原因
6. 将当前的模式设置为supervisor
7. 将`stvec`的值复制到`pc`
8. 开始执行`pc`指向的trap handler的代码

注意CPU并没有切换到kernel页表，也没有切换到kernel栈

----------------------

### Traps  from user space

当user space中发生trap时，会将`stvec`的值复制到`pc`，而此时`stvec`的值是`trampoline.S`中的`uservec`，因此跳转到`uservec`，先保存一些现场的寄存器，恢复kernel栈指针、kernel page table到`satp`寄存器，再跳转到`usertrap` trap handler，然后返回`usertrapret`，跳回到kernel/trampoline.S，最后用`userret`通过`sret`跳回到user space

RISC-V在trap中不会改变页表，因此user page table必须有对`uservec`的mapping，`uservec`是`stvec`指向的trap vector instruction。`uservec`要切换`satp`到kernel页表，同时kernel页表中也要有和user页表中对`uservec`相同的映射。RISC-V将`uservec`保存在*trampoline*页中，并将`TRAMPOLINE`放在kernel页表和user页表的相同位置处（MAXVA)

当`uservec`开始时所有的32个寄存器都是trap前代码的值，但是`uservec`需要对某些寄存器进行修改来设置`satp`，可以用`sscratch`和`a0`的值进行交换，交换之前的`sscratch`中是指向user process的`trapframe`的地址，`trapframe`中预留了保存所有32个寄存器的空间。`p->trapframe`保存了每个进程的`TRAPFRAME`的物理空间从而让kernel页表也可以访问该进程的trapframe

当交换完`a0`和`sscratch`之后，`uservec`可以通过`a0`把所有当前寄存器的值保存到trapframe中。由于当前进程的trapframe已经保存了当前进程的kernel stack、当前CPU的hartid、`usertrap`的地址、kernel page table的地址等，`uservec`需要获取这些值，然后切换到kernel pagetable，调用`usertrap`

`usertrap`主要是判断trap产生的原因并进行处理，然后返回。因为当前已经在kernel里了，所以这时候如果再发生trap，应该交给`kernelvec`处理，因此要把`stvec`切换为`kernelvec`。如果trap是一个system call，那么`syscall`将被调用，如果是设备中断，调用`devintr`，否则就是一个exception，kernel将杀死这个出现错误的进程

回到user space的第一步是调用`usertrapret()`，这个函数将把`stvec`指向`uservec`，从而当回到user space再出现trap的时候可以跳转到`uservec`，同时设置`p->trapframe`的一些值为下一次trap作准备，比如设置`p->trapframe->kernel_sp = p->kstack + PGSIZE`。清除`SPP`为从而使得调用`sret`后能够回到user mode。设置回到user space后的program counter为`p->trapframe->epc`，最后调用跳转到TRAMPOLINE页上的`userret`回到trampoline.S，加载user page table。`userret`被`userrapret`调用返回时a0寄存器中保存了TRAPFRAME，因此可以通过这个TRAPFRAME地址来恢复之前所有寄存器的值(包括a0)，最后把TRAPFRAME保存在sscratch中，用`sret`回到user space

---------------

### Calling system calls

user调用`exec`执行system call的过程：把给`exec`使用的参数放到a0和a1寄存器中，把system call的代码(SYS_exec)放到a7寄存器中，`ecall`指令将陷入内核中（通过usys.pl中的entry)。`ecall`的效果有三个，包括将CPU从user mode切换到supervisor mode、将`pc`保存到`epc`以供后续恢复、将`uservec`设置为`stvec`，并执行`uservec`、`usertrap`，然后执行`syscall`。kernel trap code将把寄存器的值保存在当前进程的trapframe中。syscall将把trapframe中的a7寄存器保存的值提取出来，索引到`syscalls`这个函数数列中查找对应的syscall种类，并进行调用，然后把返回值放置在`p->trapframe->a0`中，如果执行失败，就返回-1。

syscall的argument可以用`argint`、`argaddr`、`argfd`等函数从内存中取出

----------------

### Traps from kernel space

当执行kernel code发生CPU trap的时候，`stvec`是指向`kernelvec`的汇编代码的。`kernelvec`将寄存器的值保存在被中断的kernel thread的栈里而不是trapframe里，这样当trap需要切换kernel thread时，再切回来之后还可以从原先的thread栈里找到之前的寄存器值。

保存完寄存器之后，跳转到`kerneltrap`这个trap handler。`kerneltrap`可以对设备中断和exception这两种trap进行处理。如果是设备中断，调用`devintr`进行处理，如果是exception就panic，如果是因为计时器中断，就调用`yield`让其他kernel thread运行

最后返回到`kernelvec`中，`kernelvec`将保存的寄存器值从堆栈中弹出，执行`sret`，将`sepc`复制到`pc`来执行之前被打断的kernel code

--------------

### Lab 4: Traps

* 当调用sys_sleep时打印出函数调用关系的backtrace，即递归地打印每一个函数以及调用这个函数及其父函数的return address  
  * gcc将当前执行的函数的frame pointer存储在s0寄存器中，当前函数的return address位于fp-8的位置，previous frame pointer位于fp-16的位置。
  * 每个kernel stack都是一页，因此可以通过计算`PGROUNDDOWN(fp)`和`PGROUNDUP(fp)`来确定当前的fp地址是否还位于这一页内，从而可以是否已经完成了所有嵌套的函数调用的backtrace。
* 添加一个新的system call`sigalarm(interval, handler)`，`interval`是一个计时器的tick的间隔大小，`handler`是指向一个函数的指针，这个函数是当计时器tick到达`interval`时触发的函数。
  * 在proc结构体中添加几个成员。`int interval`保存的定时器触发的周期，`void (*handler)()`指向handler函数的指针，这两个成员变量在`sys_sigalarm`中被保存。`ticks`用来记录从上一次定时器被触发之后到目前的ticks，`in_handler`记录当前是否在handler函数中，用来防止在handler函数的过程中定时被触发再次进入handler函数
  * `sys_sigalarm`，需要将从userfunction传入的两个参数分别保存到`p`结构体相应的成员变量中
  * `sys_sigreturn`，在handler函数完成之后被调用，用于恢复现场的寄存器，并且将`in_handler`这个flag置0
  * `usertrap()`是对陷入定时器中断的处理，需要判断`which_dev==2`才是定时器中断，当`p->ticks`到达预设值`p->interval`时，将调用`p->handler`，这是通过向`p->trapframe->epc`加载handler地址实现的，这样当从`usertrap()`中返回时，`pc`在恢复现场时会加载`p->trapframe->epc`，这样就会跳转到handler地址。在跳转到handler之前先要保存`p->trapframe`的寄存器，因为执行handler函数会导致这些寄存器被覆盖。
