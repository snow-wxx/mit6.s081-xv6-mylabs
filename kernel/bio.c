// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)      //扫描缓冲区列表，查找具有给定设备和扇区号的缓冲区。
{
  struct buf *b;

  acquire(&bcache.lock); // 确保不变量，检查块是否存在以及（如果不存在）指定一个缓冲区来存储块具有原子性

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);      //bcache.lock保护有关缓存哪些块的信息
      acquiresleep(&b->lock);  //如果存在这样的缓冲区，bget将获取缓冲区的睡眠锁。 睡眠锁保护块缓冲内容的读写
      return b;  //返回锁定的缓冲区
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {   //查找未在使用中的缓冲区
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;  //确保了bread将从磁盘读取块数据，而不是错误地使用缓冲区以前的内容
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");  //如果所有缓冲区都处于忙碌，那么太多进程同时执行文件系统调用；bget将会panic
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);   //调用bget为给定扇区获取缓冲区
  if(!b->valid) {   //字段valid表示缓冲区是否包含块的副本
    virtio_disk_rw(b, 0);  //如果缓冲区需要从磁盘进行读取，在返回缓冲区之前调用virtio_disk_rw来执行此操作。
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)   //如果调用者确实修改了缓冲区，则必须在释放缓冲区之前调用bwrite将更改的数据写入磁盘
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);  //调用virtio_disk_rw与磁盘硬件对话
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)   //释放缓冲区 
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);  // 释放睡眠锁

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.   将缓冲区移动到链表的前面 移动缓冲区会使列表按缓冲区的使用频率排序
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


