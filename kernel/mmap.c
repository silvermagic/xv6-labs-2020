#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"

#define MAP_FAILED 0xffffffffffffffff

uint64     do_mmap(struct file *, uint64, uint64, int, int, uint64);
void       do_munmap(struct vm_area_struct *, uint64, uint64);
int        do_mmap_copy(pagetable_t, pagetable_t, uint64, uint64);
void       put_mmap(struct proc *, struct vm_area_struct *);
uint64     get_unmapped_area(uint64);
int        argfd(int n, int *pfd, struct file **pf);

uint64 sys_mmap()
{
  uint64 vaddr;
  int len, prot, flags, offset;
  struct file *f;

  if (argaddr(0, &vaddr) < 0 || argint(1, &len) < 0
      || argint(2, &prot) < 0 || argint(3, &flags) < 0
      || argfd(4, 0, &f) < 0 || argint(5, &offset) < 0)
    return MAP_FAILED;

  if ((vaddr = do_mmap(f, vaddr, (uint64)len, prot, flags, offset)) == MAP_FAILED)
    return MAP_FAILED;

  return vaddr;
}

uint64 sys_munmap()
{
  uint64 vaddr;
  int len;

  if (argaddr(0, &vaddr) < 0 || argint(1, &len) < 0)
    return -1;

  struct proc *p = myproc();
  struct vm_area_struct *vma;
  for (vma = p->head.vm_next; vma != &p->head; vma = vma->vm_next) {
    if (vaddr >= vma->vm_start && vaddr < vma->vm_end)
      break;
  }
  if (vma == &p->head) {
    printf("do_munmap: out of bounds\n");
    return -1;
  }
  if ((vaddr + len) > vma->vm_end) {
    printf("do_munmap: out of bounds\n");
    return -1;
  }

  do_munmap(vma, vaddr, (uint64)len);
  if (vma->vm_start == vma->vm_end) {
    fileclose(vma->file);
    vma->vm_next->vm_prev = vma->vm_prev;
    vma->vm_prev->vm_next = vma->vm_next;
    vm_area_free(vma);
  }
  return 0;
}

int do_no_page(struct vm_area_struct *vma, uint64 vaddr) {
  char *mem;
  struct proc *p = myproc();

  if((mem = kalloc()) == 0)
    return -1;
  memset(mem, 0, PGSIZE);
  if (filemap_nopage(vma, vaddr, (uint64)mem) != 0)
    goto err;

  int perm = PTE_U;
  if (vma->prot & PROT_READ)
    perm |= PTE_R;
  if (vma->prot & PROT_WRITE)
    perm |= PTE_W;
  if (vma->prot & PROT_EXEC)
    perm |= PTE_X;
  if (mappages(p->pagetable, vaddr, PGSIZE, (uint64)mem, perm) != 0)
    goto err;

  return 0;
  err:
  kfree(mem);
  return -1;
}

int mmap_copy(struct proc *old, struct proc *new) {
  struct vm_area_struct *vma, *slot;
  for (vma = old->head.vm_next; vma != &old->head; vma = vma->vm_next) {
    if ((slot = vm_area_alloc()) == 0)
      goto err;
    if (do_mmap_copy(old->pagetable, new->pagetable, vma->vm_start, vma->vm_end) != 0)
      goto err;
    slot->vm_start = vma->vm_start;
    slot->vm_end = vma->vm_end;
    slot->prot = vma->prot;
    slot->flags = vma->flags;
    slot->pgoff = vma->pgoff;
    slot->file = filedup(vma->file);
    put_mmap(new, slot);
  }

  return 0;

err:
  proc_freemmap(new);
  return -1;
}

void proc_freemmap(struct proc *p) {
  struct vm_area_struct *vma;
  while ((vma = p->head.vm_next) != &p->head) {
    p->head.vm_next = vma->vm_next;
    vma->vm_next->vm_prev = &p->head;

    do_munmap(vma, vma->vm_start, vma->vm_end - vma->vm_start);
    fileclose(vma->file);
    vm_area_free(vma);
  }
}

uint64 do_mmap(struct file *file, uint64 addr, uint64 len, int prot, int flags, uint64 pgoff) {
  struct proc *p = myproc();

  if ((len = PGROUNDUP(len)) == 0)
    return MAP_FAILED;
  if (addr != 0 || pgoff != 0 || len > (TRAPFRAME - p->sz))
    return MAP_FAILED;
  if (flags & MAP_SHARED) {
    if ((file->writable == 0) && (prot & PROT_WRITE))
      return MAP_FAILED;
  }

  addr = get_unmapped_area(len);
  struct vm_area_struct *vma = vm_area_alloc();
  if (vma == 0)
    return MAP_FAILED;

  vma->vm_start = addr;
  vma->vm_end = vma->vm_start + len;
  vma->prot = prot;
  vma->flags = flags;
  vma->pgoff = pgoff;
  vma->file = filedup(file);

  put_mmap(p, vma);
  return addr;
}

void do_munmap(struct vm_area_struct *vma, uint64 vaddr, uint64 len) {
  uint64 a;
  pte_t *pte;
  struct proc *p = myproc();

  if((vaddr % PGSIZE) != 0 || (len % PGSIZE) != 0)
    panic("do_munmap: not aligned");

  for(a = vaddr; a < vaddr + len; a += PGSIZE){
    if((pte = walk(p->pagetable, a, 0)) == 0)
      panic("do_munmap: walk");
    if((*pte & PTE_V) == 0)
      continue;
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("do_munmap: not a leaf");
    uint64 pa = PTE2PA(*pte);
    if ((vma->flags & MAP_SHARED) && (*pte & PTE_D) && filemap_sync(vma, a, pa) != 0)
      panic("do_munmap: sync");
    kfree((void*)pa);
    *pte = 0;
  }
  vma->vm_start = vaddr + len;
}

int do_mmap_copy(pagetable_t old, pagetable_t new, uint64 start, uint64 end) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = start; i < end; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("mmap_copy: pte should exist");
    if((*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, start, (end - start) / PGSIZE, 1);
  return -1;
}

void put_mmap(struct proc *p, struct vm_area_struct *vma) {
  struct vm_area_struct *slot;
  for (slot = p->head.vm_next; slot != &p->head; slot = slot->vm_next) {
    if (slot->vm_start < vma->vm_start)
      break;
  }
  slot->vm_prev->vm_next = vma;
  vma->vm_prev = slot->vm_prev;
  vma->vm_next = slot;
  slot->vm_prev = vma;
}

uint64 get_unmapped_area(uint64 len) {
  struct proc *p = myproc();

  uint64 start = p->sz, end = TRAPFRAME;
  if (start - end < len)
    return MAP_FAILED;

  struct vm_area_struct *vma;
  for (vma = p->head.vm_next; vma != &p->head; vma = vma->vm_next) {
    if (end - vma->vm_end >= len)
      break;
    end = vma->vm_start;
  }
  if (start - end < len)
    return MAP_FAILED;

  return end - len;
}

