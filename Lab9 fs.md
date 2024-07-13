## Lecture 9 File system

文件系统：组织并存储数据。

文件系统的特性：

- 需要一个存储文件夹和文件的数据结构来记录存储文件内容的硬盘块的ID，并且记录磁盘的哪些部分是空闲的
- 在不同的用户和应用程序之间共享数据
- 数据在重启/意外崩溃之后依然保持原样
- 由于访问硬盘速度远慢于访问内存，因此文件系统必须在内存里设置一个对经常访问的文件内容的缓存区。

xv6文件系统的组织架构：

- disk层：对virtio硬盘上的文件块进行读写操作
- buffer cache层：对磁盘文件块进行缓存，并确保只有1个内核进程能在一段时间内修改文件块上存储的数据。
- logging层：让更高的层级能够将对文件块的所有update打包到一个*transaction*中，从而能保证所有文件块能够在将要崩溃时原子地进行update
- inode层：为每个文件提供一个独一无二的*inode number*
- directory层：将每个文件夹作为一个特殊的inode，这个inode的内容是文件夹entry
- pathname层：将文件夹组织为层级，并通过递归查找来解析路径
- file descriptor层：将管道、设备等UNIX资源用文件系统进行抽象

--------------------------

### Disk layer

文件系统将磁盘分为了几个部分，每个部分的最小单元是block，一个block的大小为1024字节，![image-20210207200046544](https://fanxiao.tech/img/posts/MIT_6S081/image-20210207200046544.png)

- block 0: 启动区域，文件系统不会使用，包含了操作系统启动所需要的代码
- blcok 1: *superblock*，存储了文件系统的元数据（block的大小、block的数目、inode的数目等），里面有一个`mkfs`的程序，用来构建初始的文件系统
- block 2-31：log block
- block 32-44: inode，一个inode的大小为64字节，一个block的大小为1024字节，因此block32为inode 1-16，block33为inode 17-32
- block 45 bitmap block，用来跟踪哪些block是在使用
- 最后从block 46开始是data block，要么是在bitmap中被标记为空闲状态，要么存储了文件/文件夹的内容

----------------

### Buffer cache layer

buffer cache层有两个作用：

1. 将对磁盘块的访问权限进行同步，保证内存中只保存一个该磁盘块的拷贝，且一次只有一个内核线程访问这个拷贝，但同时可以有多个对这个block的引用
2. 将被频繁访问的块缓存到内存中

buffer cache(`bcache`结构体)中的`buf`数量是一定的(`NBUF`)，因此当新的文件块需要加入缓冲区时，需要将最早使用的缓冲区中的文件块替换为新的文件块。缓冲区的使用早晚通过`head`来判断。

buffer cache本身是一个双向链接的链表，链表元素为`buf`结构体，buffer cache层的接口函数有2个，分别是`bread()`和`bwrite()`

* `bread`通过`bget`获取一个指定了设备`dev`和`blockno`的`buf *`，这是从硬盘指定的块中获取的一个缓冲数据结构体，保存在内存中，可以进行修改
* `bget()`先查看需要buffer的文件块是否已经在`bcache`中，如果没有，就将LRU(least recently used) `buf`替换为这个新的文件块。`b->valid=0`说明这个`buf`是刚刚被替换掉的而不是本来就有的，因此要让`bread()`从硬盘中再加载一下相应block中的数据到这个`buf`中。`bcache.lock`负责保护哪些block被缓存的信息，而`b->lock`负责对这个缓存块的读写行为进行保护。`acquiresleep`是获取这个锁之后立即让这个进程进入睡眠，这是因为当获取着锁的时候会disable掉中断，这样就永远也无法听到来自硬盘的中断。
* 向硬盘指定块中写入数据。`struct buf`中已经保存了`dev`和`blockno`等数据，因此可以直接调用`virtio_disk_rw(b,1)`进行写入。`brelse`负责释放`bread`中返回的`buf`的锁。当发现指向这个`buf`的reference变为0时，将其移动到双向链表的最开头。

------------------

### block allocator

文件和文件夹都存储在磁盘块中，磁盘块必须从一个空闲池中进行分配。block allocator为磁盘的是否空闲的状态准备了一个bitmap，每一位对应一个磁盘块，0表示空闲1表示正在使用，`mkfs`负责设置这些位。

`balloc`负责分配新的磁盘块，`bfree`负责释放磁盘块

`balloc`最外层循环读取每个bitmap bit所代表的block（`BPB`是一个Block的bit数目，`BBLOCK`负责把bit转化为blockno）,内循环负责检查所有`BPB`位，查看这个block是否空闲

-------------------

### inode layer

*node*可能指代2种数据结构：1. 储存在硬盘上的数据结构，包含了inode类型、inode指向的文件/文件夹大小、一个数据blockno的列表 2. 存储在内存中的数据结构，拥有on-disk inode的拷贝以及其他kernel需要的metadata

on-disk inode放在一个连续的区域内，这个区域有很多block，叫做inode block，每个inode大小相同，为64字节

内存中的inode是active inodes，所谓active inodes，是指内存中有C指针指向了这个inode，`ref`是指向这个inode的指针数量，当`ref==0`时kernel将把这个inode从内存中剔除。`iget`函数和`iput`函数实现对inode pointer的获取和释放。

在inode层中总共有4种lock：

1. `icache.lock`负责确保1个inode只在cache中出现最多1次 ，并且保证`ref`正确记录引用到这个inode的数量，因此对`ref`的修改都需要用`icache.lock`进行保护 
2.  每个inode自己也有1个`lock`来保护对inode成员变量以及inode指向的文件或文件夹的内容的保护 
3.  `ref`是内存中指向这个inode的个数，当`ref`大于0时，不会将这个inode从icache中剔除 
4. `nlink`这个成员变量在on-disk inode中也存在，统计指向这个文件的directory entry的个数，当为0时将释放掉这个inode

典型的调用顺序：

```c++
ip = iget(dev, inum);
ilock(ip);
...examine and modify ip->xxx
iunlock(ip);
iput(ip);
```

`iget`返回了一个直到调用`iput`都有效的inode，任何代码均可同时访问，因此可以有很多指针指向同一个inode。`iget`返回的inode可能没有任何有用的内容(`valid==0`)，因此需要调用`ilock`来从硬盘中读取内容，并将获取inode的锁。`iunlock`释放inode的锁。将对inode的引用获取和对inode上锁分离开来，从而在查找路径等情况中避免死锁。

inode cache主要目的是实现不同进程对inode访问的同步，cache只是次要的，因为当inode被经常访问时，这个inode很可能会被保存在buffer cahce中。inode cache是*write-through*的，当cache中的inode被修改，将会立即通过`iupdate`将修改写入到磁盘中。

-----------------

**xv6中的buffer cache采用了一个非常简单的链表来对LRU进行剔除**，但是实际的操作系统中采用了hash表和heap来进行LRU剔除，且加入了虚拟内存系统来支持memory-mapped文件。

在目录树中采用了线性扫描disk block的方式进行查找，在disk block较多的情况下会消耗很多时间，因此Windows的NTFS等文件系统将文件夹用balanced tree进行表示，以确保对数事件的查找。

xv6要求文件系统只能有一个硬盘设备，但是现代操作系统可以采用RAID或者软件的方式来将很多硬盘组成一个逻辑盘

现代操作系统还应该具备的其他特性：snapshots、增量式备份。

----------------

### Logging layer

由于很多对文件系统的操作都涉及了对硬盘的多次写入，当某次写入后发生崩溃将导致文件系统出现问题。xv6通过logging来解决这个问题，xv6的syscall不会直接对硬盘上的block进行写入，而是将所有想要进行的对硬盘的写入操作的描述放到log中，当syscall将所有的写入操作都放到log后向硬盘写入一个*commit*记录来表示这个log已经记录了所有的操作，然后syscall进行全部的写入操作，并将硬盘上的log全部清除。

当操作系统崩溃后进行重启，将在进行任何进程之前从崩溃中恢复。如果在对硬盘的所有写入操作commit之前发生了崩溃，那么这个log将被视为不完整的log，xv6将直接忽略这个log，如果崩溃发生在commit之后，说明这个log是完整的，则恢复系统将重复这些步骤，最后删除log。

log位于硬盘上的log block中，由一个header block和后面的一系列被log的block的copy组成。header block中记录了所有被log的block的blockno和log block的总数`count`。xv6只有在一个transaction commits时才向header block写入，并在将logged block copy写入到文件系统中的logged block后将`count`归零。

为了支持不同的进程对文件系统同时的操作，可以将多个syscall对硬盘的写入打包到一个transaction当中，因此commit必须保证当前没有syscall

*group commit*可以将多个不同进程的syscall的transaction放在一起进行commit。

由于log block有数量限制，因此一个syscall能够写入的block数量也同样有限制，比如`sys_write`将一个write分成了好几个transaction以fit log。

- *write ahead*规则：只有所有被修改的buffer都被写入到了log block才能开始向文件系统中的home location写入block
- *freeing*规则：直到所有的log block都被写入了home location，并且消除了header block，才能开始修改或者释放log block

`commit`分成四个阶段:

* `write_log`将所有被修改了的block(`buf`)写入到log block中
* `write_head`将header block写入到硬盘中，其中包括log block的总数`log.lh.n`和每个log block接下来需要写入到的data block的blockno，这是一个commit真正开始的节点。
* `install_trans`将log block写入到需要写入的home data block中
* 最后将log header中的count变为0

-----------------------------

### Lab 9: file system

* Large files：增大xv6文件的最大大小，由于原来的大小是12个DIRECT  BLOCK+256个INDIRECT BLOCK，现在要将一个DIRECT BLOCK变为一个DOUBLE INDIRECT  BLOCK，即像page table一样的双层结构，从而使得一个文件的大小最高位256*256+256+11=65803 bytes。
* Symbolic links：实现xv6的软链接，即新增加一种`T_SYMLINK`类型的文件，这个文件中存有需要链接到的文件的pathname，当使用`open`并指定`O_NOFOLLOW`为0时，可以打开这个软链接文件指向的文件，而非软链接文件本身。要求实现一个`symlink(char *target, char *path)`这个syscall，实现`path`所代表的文件软链接到`target`代表的文件，`target`不存在也可以。修改`open`，从而可以打开软链接文件。注意：软链接文件指向的文件可能也是一个软链接文件，`open`需要递归地找到最终的非软链接文件，但是可能出现软链接文件互相链接的情况，会产生死循环，因此规定递归查找软链接的深度不能超过10.
