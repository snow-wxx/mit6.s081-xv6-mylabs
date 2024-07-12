## Lecture 1 Introduction

### **system call**

* `fork`：形式：`int fork()`。其作用是让一个进程生成另外一个和这个进程的内存内容相同的子进程。在父进程中，`fork`的返回值是这个子进程的PID，在子进程中，返回值是0，尽管`fork`了之后子进程和父进程有相同的内存内容，但是内存地址和寄存器是不一样的，也就是说在一个进程中改变变量并不会影响另一个进程。
* `exit`：形式：`int exit(int status)`。让调用它的进程停止执行并且将内存等占用的资源全部释放。需要一个整数形式的状态参数，0代表以正常状态退出，1代表以非正常状态退出
* `wait`：形式：`int wait(int *status)`。等待子进程退出，返回子进程PID，子进程的退出状态存储到`int *status`这个地址中。如果调用者没有子进程，`wait`将返回-1
* `exec`：形式：`int exec(char *file, char *argv[])`。加载一个文件，获取执行它的参数，执行。如果执行错误返回-1，执行成功则不会返回，而是开始从文件入口位置开始执行命令。文件必须是ELF格式。

------------------------------

### **I/O and File descriptors**

* ***file descriptor***：文件描述符，用来表示一个被内核管理的、可以被进程读/写的对象的一个整数，表现形式类似于字节流，通过打开文件、目录、设备等方式获得。一个文件被打开得越早，文件描述符就越小。每个进程都拥有自己独立的文件描述符列表，其中**0是标准输入，1是标准输出，2是标准错误**。shell将保证总是有3个文件描述符是可用的
* `read`和`write`：形式`int write(int fd, char *bf, int n)`和`int read(int fd, char *bf, int n)`。从/向文件描述符`fd`读/写n字节`bf`的内容，返回值是成功读取/写入的字节数。每个文件描述符有一个offset，`read`会从这个offset开始读取内容，读完n个字节之后将这个offset后移n个字节，下一个`read`将从新的offset开始读取字节。`write`也有类似的offset
* `close`。形式是`int close(int fd)`，将打开的文件`fd`释放，使该文件描述符可以被后面的`open`、`pipe`等其他system call使用。使用`close`来修改file descriptor table能够实现I/O重定向
* `dup`。形式是`int dup(int fd)`，复制一个新的`fd`指向的I/O对象，返回这个新fd值，两个I/O对象(文件)的offset相同, **除了`dup`和`fork`之外，其他方式不能使两个I/O对象的offset相同，比如同时`open`相同的文件**

----------------------

### **Pipes**

*pipe*：管道，暴露给进程的一对文件描述符，一个文件描述符用来读，另一个文件描述符用来写，将数据从管道的一端写入，将使其能够被从管道的另一端读出

`pipe`是一个system call，形式为`int pipe(int p[])`，`p[0]`为读取的文件描述符，`p[1]`为写入的文件描述符

---------------------

### File system

xv6文件系统包含了*文件*(byte arrays)和*目录*(对其他文件和目录的引用)。目录生成了一个树，树从根目录`/`开始。对于不以`/`开头的路径，认为是是相对路径

- `mknod`：创建设备文件，一个设备文件有一个major device #和一个minor device #用来唯一确定这个设备。当一个进程打开了这个设备文件时，内核会将`read`和`write`的system call重新定向到设备上。
- 一个文件的名称和文件本身是不一样的，文件本身，也叫*inode*，可以有多个名字，也叫*link*，每个link包括了一个文件名和一个对inode的引用。一个inode存储了文件的元数据，包括该文件的类型(file, directory or device)、大小、文件在硬盘中的存储位置以及指向这个inode的link的个数
- `fstat`。一个system call，形式为`int fstat(int fd, struct stat *st)`，将inode中的相关信息存储到`st`中。
- `link`。一个system call，将创建一个指向同一个inode的文件名。`unlink`则是将一个文件名从文件系统中移除，只有当指向这个inode的文件名的数量为0时这个inode以及其存储的文件内容才会被从硬盘上移除
