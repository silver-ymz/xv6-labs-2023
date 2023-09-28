#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  uint64 begin_addr, store_addr;
  int n;
  char bitmasks[8];
  pagetable_t pagetable = myproc()->pagetable;
  pte_t *entry;

  argaddr(0, &begin_addr);
  argint(1, &n);
  argaddr(2, &store_addr);

  if (n > 64) {
    return -1;
  }

  memset(bitmasks, 0, 8);

  for (int i = 0; i < n; i++) {
    entry = walk(pagetable, begin_addr + i * PGSIZE, 0);
    if (entry != 0 && (*entry & PTE_A)) {
      bitmasks[i / 8] |= 1 << (i % 8);
      *entry ^= PTE_A;
    }
  }

  copyout(pagetable, store_addr, bitmasks, n / 8);
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}