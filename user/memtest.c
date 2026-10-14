// memtest.c -- Assignment 8, Q2: physical memory statistics.
//   1. print statistics            2. grow memory (sbrk + malloc) and print again
//   3. fork a child, show how free_pages changes while it is alive
//   4. leak check: free pages after wait() must equal the value before fork()
#include "types.h"
#include "stat.h"
#include "user.h"
#include "meminfo.h"

#define PG 4096

static int
stats(char *label)
{
  struct meminfo m;
  if(getmeminfo(&m) < 0){
    printf(1, "getmeminfo failed\n");
    exit();
  }
  printf(1, "[%s]\n", label);
  printf(1, "   total_pages=%d free_pages=%d user_pages=%d resident=%d\n",
         m.total_pages, m.free_pages, m.user_pages, m.resident_pages);
  printf(1, "   pt_pages=%d (user_pt=%d kernel_pt=%d, +1 page directory)\n",
         m.pt_pages, m.user_pt_pages, m.kernel_pt_pages);
  return m.free_pages;
}

// Touch one byte per page so the memory is really backed by frames,
// whether sbrk() is eager or lazy.
static void
touch(char *p, int bytes)
{
  int i;
  for(i = 0; i < bytes; i += PG)
    p[i] = 1;
}

int
main(void)
{
  int f0, f1, f2, f3, fbefore, fduring, fafter, pid, i, leak = 0;
  char *p, *q;

  printf(1, "==== Q2: memtest ====\n");
  f0 = stats("1. start");

  p = sbrk(64 * PG);                 // 256 KB via sbrk
  touch(p, 64 * PG);
  f1 = stats("2a. after sbrk(64 pages) + touching them");
  printf(1, "   -> free pages dropped by %d (64 data pages; the existing page table already covers them)\n", f0 - f1);

  q = malloc(40 * PG);               // 160 KB via malloc (calls sbrk internally)
  touch(q, 40 * PG);
  f2 = stats("2b. after malloc(40 pages) + touching them");
  printf(1, "   -> free pages dropped by %d\n", f1 - f2);

  fbefore = stats("3. just before fork()");
  pid = fork();
  if(pid < 0){
    printf(1, "fork failed\n");
    exit();
  }
  if(pid == 0){
    sleep(30);                       // stay alive while the parent measures
    exit();
  }
  fduring = stats("3. parent, child still alive");
  printf(1, "   -> fork() consumed %d pages (copy of user pages + page tables + pgdir + kernel stack)\n",
         fbefore - fduring);
  wait();
  fafter = stats("4. after child exited and was reaped by wait()");
  printf(1, "   -> free pages restored: before fork=%d after wait=%d : %s\n",
         fbefore, fafter, fbefore == fafter ? "NO LEAK (PASS)" : "LEAK (FAIL)");

  // Stress the leak check: 20 fork/wait cycles must not change free_pages.
  for(i = 0; i < 20; i++){
    pid = fork();
    if(pid == 0)
      exit();
    wait();
  }
  f3 = stats("5. after 20 more fork()/wait() cycles");
  if(f3 != fbefore)
    leak = 1;
  printf(1, "   -> %s\n", leak ? "LEAK (FAIL)" : "NO LEAK after 20 cycles (PASS)");

  free(q);
  exit();
}
