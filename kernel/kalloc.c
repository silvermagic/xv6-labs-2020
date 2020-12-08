// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  struct vm_area_struct vma[NVMA];
  struct vm_area_struct head;
} mmap;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);

  initlock(&mmap.lock, "mmap");
  mmap.head.vm_next = &mmap.head;
  mmap.head.vm_prev = &mmap.head;
  for (int i = 0; i < NVMA; i++) {
    mmap.head.vm_next->vm_prev = &mmap.vma[i];
    mmap.vma[i].vm_next = mmap.head.vm_next;
    mmap.head.vm_next = &mmap.vma[i];
    mmap.vma[i].vm_prev = &mmap.head;
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

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

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

struct vm_area_struct *vm_area_alloc() {
  struct vm_area_struct *vma;
  acquire(&mmap.lock);
  vma = mmap.head.vm_next;
  if (vma != &mmap.head) {
    mmap.head.vm_next = vma->vm_next;
    vma->vm_next->vm_prev = &mmap.head;
    release(&mmap.lock);
    return vma;
  }
  release(&mmap.lock);
  return 0;
}

void vm_area_free(struct vm_area_struct *vma) {
  acquire(&mmap.lock);
  mmap.head.vm_next->vm_prev = vma;
  vma->vm_next = mmap.head.vm_next;
  mmap.head.vm_next = vma;
  vma->vm_prev = &mmap.head;
  release(&mmap.lock);
}