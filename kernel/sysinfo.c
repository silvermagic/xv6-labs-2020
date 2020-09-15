//
// Created by ubuntu on 2020/9/15.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_sysinfo(void) {
    uint64 addr; // user pointer to struct sysinfo

    if(argaddr(0, &addr) < 0)
        return -1;

    struct sysinfo info;
    info.freemem = kfree_count();
    info.nproc = proc_count();
    if(copyout(myproc()->pagetable, addr, (char *)&info, sizeof(info)) < 0)
        return -1;
    return 0;
}