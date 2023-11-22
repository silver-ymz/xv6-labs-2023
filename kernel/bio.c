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
  struct spinlock lock[NBUCKET];
  struct spinlock ref_lock, freelist_lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
  struct buf *freelist;
  char name[NBUCKET + 1][20];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.ref_lock, "bcache.ref_lock");
  initlock(&bcache.freelist_lock, "bcache.freelist_lock");
  for (int i = 0; i < NBUCKET; i++) {
    int off = snprintf(bcache.name[i], sizeof(bcache.name[i]), "bcache.bucket.%d", i);
    if (off < 0 || off >= sizeof(bcache.name[i])) {
      panic("binit: snprintf");
    }
    bcache.name[i][off] = '\0';
    initlock(&bcache.lock[i], bcache.name[i]);
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // Create linked list of buffers
  bcache.freelist = 0;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.freelist;
    initsleeplock(&b->lock, "buffer");
    bcache.freelist = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket = blockno % NBUCKET;

  acquire(&bcache.lock[bucket]);

  // Is the block already cached?
  for (b = bcache.head[bucket].next; b != &bcache.head[bucket]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      // Move to front of list to speed up future lookups.
      b->next->prev = b->prev;
      b->prev->next = b->next;
      b->next = bcache.head[bucket].next;
      b->prev = &bcache.head[bucket];
      bcache.head[bucket].next->prev = b;
      bcache.head[bucket].next = b;
      release(&bcache.lock[bucket]);
      acquire(&bcache.ref_lock);
      b->refcnt++;
      release(&bcache.ref_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  release(&bcache.lock[bucket]);
  acquire(&bcache.freelist_lock);
  if (!bcache.freelist) {
    panic("bget: no buffers");
  }
  b = bcache.freelist;
  bcache.freelist = b->next;
  release(&bcache.freelist_lock);
  acquire(&bcache.lock[bucket]);
  b->next = bcache.head[bucket].next;
  b->prev = &bcache.head[bucket];
  bcache.head[bucket].next->prev = b;
  bcache.head[bucket].next = b;
  release(&bcache.lock[bucket]);
  b->refcnt = 1;
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  acquiresleep(&b->lock);
  return b;
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
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.ref_lock);
  int refcnt = --b->refcnt;
  release(&bcache.ref_lock);

  if (refcnt == 0) {
    int bucket = b->blockno % NBUCKET;
    acquire(&bcache.lock[bucket]);
    b->next->prev = b->prev;
    b->prev->next = b->next;
    release(&bcache.lock[bucket]);
    acquire(&bcache.freelist_lock);
    b->next = bcache.freelist;
    bcache.freelist = b;
    release(&bcache.freelist_lock);
  }
}

void
bpin(struct buf *b) {
  acquire(&bcache.ref_lock);
  b->refcnt++;
  release(&bcache.ref_lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.ref_lock);
  b->refcnt--;
  release(&bcache.ref_lock);
}


