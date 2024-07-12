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
