## Lecture 8 Sleep & Wakeup

### sleep & wakeup

sleep是当一个进程在等待某一个事件时陷入休眠状态，当这个事件发生时另外一个进程唤醒它。陷入休眠状态可以让这个进程不在等待的时候占用CPU资源

`sleep(chan)`让这个进程睡眠在`chan`这个*wait channel*上，`wakeup(chan)`将所有睡眠在`chan`上的进程全部唤醒。

*lost wake-up problem*：当一个进程A即将睡眠时，另外一个进程B发现已经满足了唤醒它的条件进行了唤醒，但是这时还没有进程睡眠在`chan`上，当进程A开始进入睡眠后，进程B可能不会再对进程A进行唤醒，进程A永远进入睡眠状态

对*lost wake-up problem*的解决方法：用*condition lock*对`sleep`和`wakeup`前后进行保护，

```c++
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}
```

在`sleep`中，首先需要获取一个进程锁来修改这个进程的`p->chan`和`p->state`。当获取了这个进程锁之后可以立刻释放掉`lk`，因为在`wakeup`中只有先获取了`sleep`进程的进程锁才能进行尝试唤醒(判断`p->state==SLEEPING`)，因此可以保证在`sleep`中将`p->state`修改为`SLEEPING`之前是无法进行唤醒的.

注意：当`lk`本身就是`p->lock`时会出现死锁的问题。

```c++
// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}
```

唤醒进程遍历所有的进程，先获取每个进程的进程锁，然后判断每个进程是否睡眠在`chan`上，如果是则将其状态改为`RUNNABLE`，最后释放进程锁。

--------------------

### pipe & wait & exit & kill

每一个`pipe`都有一个`struct pipe`，包括了一个`lock`和一个`data`缓冲数组

`pipewrite()`：先获取`pipe`的锁，因为要对`pi`结构体里的变量进行修改。通过`pi->nwrite == pi->nread+PIPESIZE`判断缓冲区是否已经满了，如果已经满了就唤醒睡在`&pi->nread`上的`piperead`进程对缓冲区进行读取，自己睡在`&pi->nwrite`等待唤醒，否则就从user space的`addr`中`copyin`到内核态中的`pi`缓冲区内，完成n字节的读取之后将`piperead`进程唤醒，释放`&pi->lock`。

`pipread()`：先获取`pi->lock`，判断当前缓冲区内是不是空的，如果是空的就进入睡眠，等待`pipewrite`进行写入并唤醒，否则循环读取n字节缓冲区数据，将缓冲区的数据`copyout`到用户空间的`addr`地址中，待n字节数据全部读取完成之后将`pipewrite`唤醒。

------------------------------------------

`wait`中是一个无限循环，每个循环中先对所有的进程循环查找自己的子进程，当发现有子进程并且子进程的状态为`ZOMBIE`时，将子进程的退出状态`np->xstate` `copyout`到`wait`传入的用户空间的`addr`中，然后释放掉子进程占用的所有的内存空间，返回子进程的pid。如果没有发现任何`ZOMBIE`子进程，睡眠在`p`上以等待子进程`exit`时唤醒`p`。`wait_lock`是`wait`和`exit`的condition lock用来防止错过wakeup。`wait_lock`实际上就是`wait()`调用者的`p->lock`

**注意**：`wait()`先要获取调用进程的`p->lock`作为`sleep`的condition lock，然后在发现`ZOMBIE`子进程后获取子进程的`np->lock`，因此xv6中必须遵守先获取父进程的锁才能获取子进程的锁这一个规则。因此在循环查找`np->parent == p`时，不能先获取`np->lock`，因为`np`很有可能是自己的父进程，这样就违背了先获取父进程锁再获取子进程锁这个规则，可能造成死锁。

------------------

`exit`关闭所有打开的文件，将自己的子进程reparent给`init`进程，因为`init`进程永远在调用`wait`，这样就可以让自己的子进程在`exit`后由`init`进行`freeproc`等后续的操作。然后获取进程锁，设置退出状态和当前状态为`ZOMBIE`，进入`scheduler`中并且不再返回。

注意：在将`p->state`设置为`ZOMBIE`之后才能释放掉`wait_lock`，否则`wait()`的进程被唤醒之后发现了`ZOMBIE`进程之后直接将其释放，此时`ZOMBIE`进程还没运行完毕。

`exit`是让自己的程序进行退出，`kill`是让一个程序强制要求另一个程序退出。`kill`不能立刻终结另一个进程，因为另一个进程可能在执行敏感命令，因此`kill`仅仅设置了`p->killed`为1，且如果该进程在睡眠状态则将其唤醒。当被`kill`的进程进入`usertrap`之后，将会查看`p->killed`是否为1，如果为1则将调用`exit`

-----------------------

xv6采用的scheduling的方式为*round robin*，即每个进程轮流运行，实际的操作系统的scheduling可以让进程有优先级。当高优先级和低优先级共享一个锁而低优先级拿到这个锁的情况下，将产生*priority inversion*，将导致大量高优先级进程等候低优先级进程，从而形成*convoy*。

对整个进程列表查找睡眠在`chan`上的进程是非常低效的，更好的解决方案是将`chan`替代为一个可以存储睡眠在此结构体上的进程列表的结构体，比如Linux的*wait queue*

xv6中的`wakeup`唤醒所有睡在`chan`上的进程，然后这些进程将竞争检查sleep conditoin，这种情况通常需要被避免。许多是采用`signal`和`broadcast`两种唤醒模式，前面一种只唤醒一个进程，后面一种唤醒所有进程。

----------------------

### Lab 8: Locks

* Memory allocator: 实现一个per CPU freelist，以减小各个进程同时调用`kalloc`、`kfree`造成的对`kmem.lock`锁的竞争。可以调用`cpuid()`函数来获取当前进程运行的CPU ID，但是要在调用前加上`push_off`以关闭中断。

  * 修改`kmem`结构体为数组形式，`kinit()`要循环初始化每一个`kmem`的锁  
  * `kfree`将释放出来的freelist节点返回给调用`kfree`的CPU
  * 在`kalloc`中，当发现freelist已经用完后，需要向其他CPU的freelist借用节点

* Buffer cache: xv6文件系统的buffer cache采用了一个全局的锁`bcache.lock`来负责对buffer cache进行读写保护，当xv6执行读写文件强度较大的任务时会产生较大的锁竞争压力，因此需要一个哈希表，将buf entry以`buf.blockno`为键哈希映射到这个哈希表的不同的BUCKET中，给每个BUCKET一个锁，`NBUCKET`最好选择素数，这里选择13。注意：这个实验不能像上一个一样给每个CPU一个`bcache`，因为文件系统在多个CPU之间是真正实现共享的，否则将会造成一个CPU只能访问某些文件的问题。

  * 可以使用`ticks`作为时间戳，来代替原来的双向链表实现LRU的功能。

  * 为每个`bcache.lock`以及`b->lock`进行初始化，并将所有`buf`先添加到bucket 0哈希表中

  * 修改`bget`，先查找当前哈希表中有没有和传入参数`dev`、`blockno`相同的`buf`。先要将`blockno`哈希到一个id值，然后获得对应id值的`bcache.lock[id]`锁，然后在这个bucket id哈希链表中查找符合对应条件的`buf`，如果找到则返回，返回前释放掉`bcache.lock[id]`，并对`buf`加sleeplock。

    如果没有找到对应的`buf`，需要在整个哈希表中查找LRU(least recently used)`buf`，将其替换掉。这里由于总共有`NBUCKET`个哈希表，而此时一定是持有`bcache.lock[id]`这个哈希表的锁的，因此当查找其他哈希表时，需要获取其他哈希表的锁，这时就会有产生死锁的风险。风险1：查找的哈希表正是自己本身这个哈希表，在已经持有自己哈希表锁的情况下，不能再尝试`acquire`一遍自己的锁。风险2：假设有2个进程同时要进行此步骤，进程1已经持有了哈希表A的锁，尝试获取哈希表B的锁，进程2已经持有了哈希表B的锁，尝试获取哈希表A的锁，同样会造成死锁，因此要规定一个规则，当**持有哈希表A的情况下如果能够获取哈希表B的锁，则当持有哈希表B锁的情况下不能够持有哈希表A的锁**。该规则在`can_lock`函数中实现

    尝试遍历所有的哈希表，通过`b->time`查找LRU`buf`。先判断当前的哈希表索引是否为`id`(已经持有的锁)，如果是，则不获取这个锁（已经获取过它了），但是还是要遍历这个哈希表的；同时也要判断当前哈希表索引是否满足`can_lock`规则，如果不满足，则不遍历这个哈希表，直接`continue`。如果哈希表索引`j`既不是`id`，也满足`can_lock`，则获取这个锁，并进行遍历。当找到了一个当前情况下的`b->time`最小值时，如果这个最小值和上一个最小值不在同一个哈希表中，则释放上一个哈希表锁，一直持有拥有当前情况下LRU`buf`这个哈希表的锁，直到找到新的LRU`buf`且不是同一个哈希表为止。找到LRU`buf`后，由于此时还拥有这个哈希表的锁，因此可以直接将这个`buf`从该哈希链表中剔除，并将其append到bucket`id`哈希表中，修改这个锁的`dev`、`blockno`、`valid`、`refcnt`等属性。最后释放所有的锁。

  * 当`b->refcnt==0`时，说明这个`buf`已经被使用完了，可以进行释放，为其加上时间戳
  * 由于`can_lock`规则，`NBUF`实际上变成了原来的一半，可能会出现`buffer run out`的panic，无法通过`writebig`测试，因此适当增大`NBUF`

  





