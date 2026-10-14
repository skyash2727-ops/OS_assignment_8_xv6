#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "meminfo.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;
  struct proc *p = myproc();

  if(argint(0, &n) < 0)
    return -1;
  addr = p->sz;

  if(n > 0 && LAZY_ALLOC){
    // [A8-Q4] Lazy growth: only enlarge the logical size.  No frame is
    // allocated and no page-table entry is created; the page-fault handler
    // in trap.c does that on first access.  Reject overflow / reaching KERNBASE.
    if((uint)addr + (uint)n < (uint)addr || (uint)addr + (uint)n >= KERNBASE)
      return -1;
    p->sz += n;
    return addr;
  }
  if(n < 0){
    // Shrinking must give back the frames that were really allocated
    // (growproc -> deallocuvm skips PTEs that were never faulted in).
    // Never allow shrinking into the stack / guard page / program image.
    if((uint)(-n) > (uint)addr || (uint)(addr + n) < p->heapstart)
      return -1;
  }
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// [A8-Q2] int getmeminfo(struct meminfo *mi)
// Fills *mi with system-wide and per-process memory statistics.
// copyout() is used (not a direct store) so that a destination page that is
// not mapped yet (lazy allocation, Q4) is handled correctly.
int
sys_getmeminfo(void)
{
  struct meminfo mi;
  struct proc *p = myproc();
  char *umi;

  if(argptr(0, &umi, sizeof(mi)) < 0)
    return -1;
  mi.total_pages = ktotalpages();
  mi.free_pages  = kfreepages();
  vmmeminfo(p->pgdir, p->sz, &mi);
  if(copyout(p->pgdir, (uint)umi, &mi, sizeof(mi)) < 0)
    return -1;
  return 0;
}

// [A8-Q3] int vmprint(void): dump the calling process's page tables to the console.
int
sys_vmprint(void)
{
  return vmdump(myproc());
}
