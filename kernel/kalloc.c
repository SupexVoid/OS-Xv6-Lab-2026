// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

static struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define NPHYPAGES ((PHYSTOP - KERNBASE) / PGSIZE)

static struct {
  struct spinlock lock;
  int count[NPHYPAGES];
} krefs;

static int
refindex(void *pa)
{
  uint64 address = (uint64)pa;

  if(address < KERNBASE || address >= PHYSTOP || address % PGSIZE != 0)
    panic("refindex");
  return (address - KERNBASE) / PGSIZE;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&krefs.lock, "krefs");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    acquire(&krefs.lock);
    krefs.count[refindex(p)] = 1;
    release(&krefs.lock);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int refs;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&krefs.lock);
  refs = --krefs.count[refindex(pa)];
  if(refs < 0)
    panic("kfree refs");
  release(&krefs.lock);

  // Other page tables still refer to this physical page.
  if(refs > 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    acquire(&krefs.lock);
    if(krefs.count[refindex(r)] != 0)
      panic("kalloc refs");
    krefs.count[refindex(r)] = 1;
    release(&krefs.lock);
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}

// Add another page-table reference to an allocated physical page.
void
krefinc(void *pa)
{
  int index = refindex(pa);

  acquire(&krefs.lock);
  if(krefs.count[index] < 1)
    panic("krefinc");
  krefs.count[index]++;
  release(&krefs.lock);
}

int
krefcount(void *pa)
{
  int refs;

  acquire(&krefs.lock);
  refs = krefs.count[refindex(pa)];
  if(refs < 1)
    panic("krefcount");
  release(&krefs.lock);
  return refs;
}
