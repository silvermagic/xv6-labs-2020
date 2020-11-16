//
// Created by kyle on 2020/11/16.
//

#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

extern struct {
  struct spinlock lock;
  int mem_map[(PHYSTOP - KERNBASE) >> PGSHIFT];
} umem;

extern struct run {
  struct run *next;
} run;

extern struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

int kmem_mask[(PHYSTOP - KERNBASE) >> PGSHIFT];
uint64 sys_kprint() {
  struct run *r;
  for (int i=0; i<((PHYSTOP - KERNBASE) >> PGSHIFT); i++)
    kmem_mask[i] = 0;

  acquire(&kmem.lock);
  r = kmem.freelist;
  while(r) {
    kmem_mask[((uint64)r - KERNBASE) >> PGSHIFT] = 1;
    r = r->next;
  }
  for (int i = 0; i < ((PHYSTOP - KERNBASE) >> PGSHIFT); i += 128) {
    printf("%p: ", KERNBASE + i * PGSIZE);
    for (int j = 0; j < 128; j++) {
      if (kmem_mask[i + j]) {
        printf("-");
      } else {
        printf("+");
      }
    }
    printf("\n");
  }
  release(&kmem.lock);
  return 0;
}

uint64 sys_countkfree(void){
  struct run *r;
  uint64 count = 0;
  acquire(&kmem.lock);
  r = kmem.freelist;
  while(r) {
    count++;
    r = r->next;
  }
  release(&kmem.lock);
  return count;
}

uint64 sys_pgtrace() {
  struct proc *p = myproc();
  printf("page table %p\n", p->pagetable);
  acquire(&umem.lock);
  for (int i = 0; i < 512; i++) {
    pte_t pte_l2 = p->pagetable[i];
    if (pte_l2 & PTE_V) {
      pagetable_t pagetable_l1 = (pagetable_t) PTE2PA(pte_l2);
      printf(" ..%d: pte %p pa %p\n", i, pte_l2, pagetable_l1);
      for (int j = 0; j < 512; j++) {
        pte_t pte_l1 = pagetable_l1[j];
        if (pte_l1 & PTE_V) {
          pagetable_t pagetable_l0 = (pagetable_t) PTE2PA(pte_l1);
          printf(" .. ..%d: pte %p pa %p\n", j, pte_l1, pagetable_l0);
          for (int k = 0; k < 512; k++) {
            pte_t pte_l0 = pagetable_l0[k];
            if (pte_l0 & PTE_V) {
              printf(" .. .. ..%d: pte %p va %p pa %p refs %d ", k, pte_l0, ((uint64)i << 30) | ((uint64)j << 21) | ((uint64)k << 12), (pagetable_t) PTE2PA(pte_l0), umem.mem_map[(PTE2PA(pte_l0) - KERNBASE) >> PGSHIFT]);
              if (pte_l0 & PTE_COW)
                printf("c");
              else
                printf("-");
              if (pte_l0 & PTE_U)
                printf("u");
              else
                printf("-");
              if (pte_l0 & PTE_R)
                printf("r");
              else
                printf("-");
              if (pte_l0 & PTE_W)
                printf("w");
              else
                printf("-");
              printf("\n");
            }
          }
        }
      }
    }
  }
  release(&umem.lock);
  return 0;
}