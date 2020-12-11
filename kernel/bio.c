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

#define NBUCKET 13
#define HASH(dev, blockno) ((((uint64)dev << sizeof(uint)) | blockno) % NBUCKET)

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  uint ticks[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
  struct bucket bucket[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  // Create linked list of buffers
  for(int i = 0; i<NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.bucket[0].head.next;
    b->prev = &bcache.bucket[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[0].head.next->prev = b;
    bcache.bucket[0].head.next = b;
  }

}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int hash = HASH(dev, blockno);
  acquire(&bcache.bucket[hash].lock);
  // Is the block already cached?
  for(b = bcache.bucket[hash].head.next; b != &bcache.bucket[hash].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[hash].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket[hash].lock);

  acquire(&bcache.lock);
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  acquire(&bcache.bucket[hash].lock);
  for(int i = (hash + 1) % NBUCKET; i != hash; i = (i + 1) % NBUCKET){
    acquire(&bcache.bucket[i].lock);
    for(b = bcache.bucket[i].head.prev; b != &bcache.bucket[i].head; b = b->prev){
      if(b->refcnt == 0){
        b->prev->next = b->next;
        b->next->prev = b->prev;
        break;
      }
    }
    release(&bcache.bucket[i].lock);
    if(b != &bcache.bucket[i].head){
      b->next = bcache.bucket[hash].head.next;
      b->prev = &bcache.bucket[hash].head;
      bcache.bucket[hash].head.next->prev = b;
      bcache.bucket[hash].head.next = b;
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bucket[hash].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  struct bucket *bucket;
  bucket = &bcache.bucket[HASH(b->dev, b->blockno)];
  acquire(&bucket->lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bucket->head.next;
    b->prev = &bucket->head;
    bucket->head.next->prev = b;
    bucket->head.next = b;
  }
  release(&bucket->lock);
}

void
bpin(struct buf *b) {
  struct bucket *bucket;
  bucket = &bcache.bucket[HASH(b->dev, b->blockno)];
  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b) {
  struct bucket *bucket;
  bucket = &bcache.bucket[HASH(b->dev, b->blockno)];
  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}


