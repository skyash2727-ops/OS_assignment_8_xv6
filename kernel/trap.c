#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  // [A8-Q4] Page fault: demand-allocate heap pages.
  case T_PGFLT: {
    uint va = rcr2();
    struct proc *p = myproc();
    int r;
    // err bit 0 == 0  <=>  page NOT present (a protection violation on a
    // present page, e.g. the stack guard page, is never fixed up).
    // This also works for faults taken in kernel mode (e.g. read()/write()
    // copying to/from an untouched user buffer): the kernel runs on the
    // process's page table, so mapping the page and retrying the faulting
    // instruction is all that is needed.
    if(p && va < KERNBASE && !(tf->err & 1)){
      r = lazyalloc(p, va);
      if(r == 0)
        break;                                   // resume faulting instruction
      if(r == -2 && (tf->cs&3) == DPL_USER){     // kalloc() failed
        cprintf("pid %d %s: lazy alloc out of memory at 0x%x -- kill proc\n",
                p->pid, p->name, va);
        p->killed = 1;
        break;
      }
    }
    if(p && (tf->cs&3) == DPL_USER){
      // Invalid user access: say why, kill only this process.
      char *why;
      if(va >= KERNBASE)                  why = "kernel address";
      else if(tf->err & 1)                why = "protection violation (e.g. stack guard page)";
      else if(va >= p->sz)                why = "address >= sz (beyond heap)";
      else                                why = "address below heap/stack region";
      cprintf("pid %d %s: segfault at 0x%x eip 0x%x: %s -- kill proc\n",
              p->pid, p->name, va, tf->eip, why);
      p->killed = 1;
      break;
    }
  }
  // fall through: unrecoverable fault inside the kernel
  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
