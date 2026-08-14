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

#define NBUCKET 31

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  // Misses and eviction are serialized; hits and releases only take one
  // bucket lock and therefore remain parallel for different block hashes.
  struct spinlock evictlock;
  struct bucket bucket[NBUCKET];
  struct buf buf[NBUF];
  uint64 clock;
} bcache;

static uint
bhash(uint dev, uint blockno)
{
  (void)dev;
  return blockno % NBUCKET;
}

static void
insert(struct bucket *bucket, struct buf *b)
{
  b->next = bucket->head.next;
  b->prev = &bucket->head;
  bucket->head.next->prev = b;
  bucket->head.next = b;
}

static void
remove(struct buf *b)
{
  b->prev->next = b->next;
  b->next->prev = b->prev;
}

static void
acquirebuckets(uint a, uint b)
{
  if(a == b){
    acquire(&bcache.bucket[a].lock);
  } else if(a < b){
    acquire(&bcache.bucket[a].lock);
    acquire(&bcache.bucket[b].lock);
  } else {
    acquire(&bcache.bucket[b].lock);
    acquire(&bcache.bucket[a].lock);
  }
}

static void
releasebuckets(uint a, uint b)
{
  if(a == b){
    release(&bcache.bucket[a].lock);
  } else if(a < b){
    release(&bcache.bucket[b].lock);
    release(&bcache.bucket[a].lock);
  } else {
    release(&bcache.bucket[a].lock);
    release(&bcache.bucket[b].lock);
  }
}

void
binit(void)
{
  initlock(&bcache.evictlock, "bcache.evict");
  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.prev = &bcache.bucket[i].head;
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  // Unassigned buffers start in bucket zero with an impossible device id.
  for(struct buf *b = bcache.buf; b < bcache.buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->dev = (uint)-1;
    insert(&bcache.bucket[0], b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  uint target = bhash(dev, blockno);
  struct bucket *bucket = &bcache.bucket[target];
  struct buf *b;

  acquire(&bucket->lock);

  // Is the block already cached?
  for(b = bucket->head.next; b != &bucket->head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket->lock);

  // A miss may change a buffer's identity. Serializing identity changes
  // makes lookup plus insertion atomic and preserves one cached copy/block.
  acquire(&bcache.evictlock);

  // Another CPU may have inserted the requested block before evictlock was
  // acquired, so repeat the lookup while insertion is globally serialized.
  acquire(&bucket->lock);
  for(b = bucket->head.next; b != &bucket->head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      release(&bcache.evictlock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket->lock);

  struct buf *victim;
  uint old;
  for(;;){
    victim = 0;
    uint64 oldest = 0;
    for(b = bcache.buf; b < bcache.buf + NBUF; b++){
      uint64 used = lockfree_read8(&b->lastuse);
      if(lockfree_read4((int*)&b->refcnt) == 0 &&
         (victim == 0 || used < oldest)){
        victim = b;
        oldest = used;
      }
    }

    if(victim == 0){
      release(&bcache.evictlock);
      panic("bget: no buffers");
    }

    old = bhash(victim->dev, victim->blockno);
    acquirebuckets(old, target);
    // A fast-path lookup may have claimed the candidate after the lock-free
    // scan. Recheck under its bucket lock and choose again if necessary.
    if(victim->refcnt == 0)
      break;
    releasebuckets(old, target);
  }

  remove(victim);
  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;
  insert(bucket, victim);

  releasebuckets(old, target);
  release(&bcache.evictlock);

  acquiresleep(&victim->lock);
  return victim;
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

  struct bucket *bucket = &bcache.bucket[bhash(b->dev, b->blockno)];
  acquire(&bucket->lock);
  b->refcnt--;
  if(b->refcnt == 0){
    uint64 used = __sync_add_and_fetch(&bcache.clock, 1);
    __atomic_store_n(&b->lastuse, used, __ATOMIC_RELEASE);
  }
  release(&bucket->lock);
}

void
bpin(struct buf *b) {
  struct bucket *bucket = &bcache.bucket[bhash(b->dev, b->blockno)];
  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b) {
  struct bucket *bucket = &bcache.bucket[bhash(b->dev, b->blockno)];
  acquire(&bucket->lock);
  b->refcnt--;
  if(b->refcnt == 0){
    uint64 used = __sync_add_and_fetch(&bcache.clock, 1);
    __atomic_store_n(&b->lastuse, used, __ATOMIC_RELEASE);
  }
  release(&bucket->lock);
}
