// lazytest.c -- Assignment 8, Q4: lazy heap allocation (demand paging).
//
//  T1  sbrk(32 MB) returns at once and consumes no frames
//  T2  touching 5 widely separated pages allocates exactly those frames
//  T3  an untouched page reads as zero (zero-fill on demand)
//  T4  fork(): child inherits touched pages only; copy-on-fork isolation; no leak
//  T5  system calls with untouched buffers: pipe write/read, file read, getmeminfo
//  T6  invalid accesses kill only the offending (child) process
//  T7  shrinking returns exactly the frames that were really allocated
//  T8  out-of-memory during lazy allocation kills the process, kernel survives
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "meminfo.h"

#define PG 4096
#define MB (1024 * 1024)

static int pass = 0, fail = 0;

static void
check(int ok, char *what)
{
  if(ok) pass++; else fail++;
  printf(1, "   %s: %s\n", ok ? "PASS" : "FAIL", what);
}

static struct meminfo
stats(char *label)
{
  struct meminfo m;
  getmeminfo(&m);
  printf(1, "[%s]\n   free_pages=%d  user_pages(sz)=%d  resident=%d  user_pt=%d\n",
         label, m.free_pages, m.user_pages, m.resident_pages, m.user_pt_pages);
  return m;
}

int
main(int argc, char *argv[])
{
  struct meminfo m0, m1, m2, m3;
  char *base, *heap, *p, *buf;
  int i, pid, fd, n, fds[2], ok;
  int offs[5] = { 0, 5*MB, 11*MB + 3*PG, 19*MB, 31*MB + 7*PG };

  base = sbrk(0);                       // = heapstart (top of the stack page)
  printf(1, "==== Q4: lazytest (LAZY heap allocation) ====\n");

  // ---------------------------------------------------------------- T1
  printf(1, "\n-- T1: sbrk(32 MB) must not allocate memory --\n");
  m0 = stats("before sbrk");
  heap = sbrk(32 * MB);
  if(heap == (char*)-1){ printf(1, "sbrk failed\n"); exit(); }
  m1 = stats("after sbrk(32MB), nothing touched");
  check(m1.free_pages == m0.free_pages, "free_pages unchanged by sbrk(32MB)");
  check(m1.user_pages - m0.user_pages == 32*MB/PG, "sz grew by 8192 pages");
  check(m1.resident_pages == m0.resident_pages, "no new resident pages");

  // ---------------------------------------------------------------- T2
  printf(1, "\n-- T2: touch 5 widely spaced pages --\n");
  for(i = 0; i < 5; i++)
    heap[offs[i] + 100] = i + 1;        // page fault -> kalloc + mappages
  m2 = stats("after touching 5 pages");
  check(m2.resident_pages - m1.resident_pages == 5, "exactly 5 pages became resident");
  check(m1.free_pages - m2.free_pages ==
        5 + (m2.user_pt_pages - m1.user_pt_pages),
        "free_pages dropped by 5 data pages + the new page-table pages");
  printf(1, "   (page-table pages added: %d, because the pages lie in different 4 MB regions)\n",
         m2.user_pt_pages - m1.user_pt_pages);
  ok = 1;
  for(i = 0; i < 5; i++)
    if(heap[offs[i] + 100] != i + 1) ok = 0;
  check(ok, "values written to the lazily mapped pages read back correctly");

  // ---------------------------------------------------------------- T3
  printf(1, "\n-- T3: untouched page reads as zero --\n");
  ok = (heap[24*MB + 77] == 0);
  m3 = stats("after READING one untouched page");
  check(ok, "byte read from untouched page is 0");
  check(m3.resident_pages - m2.resident_pages == 1, "the read faulted in exactly one frame");

  // ---------------------------------------------------------------- T4
  printf(1, "\n-- T4: fork / exit with a sparse heap --\n");
  m1 = stats("parent just before fork");
  pid = fork();
  if(pid == 0){
    ok = 1;
    for(i = 0; i < 5; i++)
      if(heap[offs[i] + 100] != i + 1) ok = 0;
    printf(1, "   child: inherited data in touched pages: %s\n", ok ? "OK" : "WRONG");
    heap[27*MB] = 99;                   // child touches a page the parent never touched
    stats("child, after fork and one private write");
    sleep(30);
    exit();
  }
  sleep(10);
  m2 = stats("parent while child is alive");
  printf(1, "   fork consumed %d frames (only touched pages were copied, not 8192)\n",
         m1.free_pages - m2.free_pages);
  check(m1.free_pages - m2.free_pages < 300, "fork of a 32MB lazy heap is cheap (<300 frames)");
  wait();
  m3 = stats("parent after child exited and was reaped");
  check(m3.free_pages == m1.free_pages, "free_pages back to the value before fork (no leak)");
  check(heap[27*MB] == 0, "parent's page unaffected by child's write (isolation)");
  m1 = stats("parent after that read (1 more page faulted in)");

  // ---------------------------------------------------------------- T5
  printf(1, "\n-- T5: system calls on untouched user buffers --\n");
  m0 = stats("before syscalls");
  pipe(fds);
  n = write(fds[1], heap + 13*MB, 64);            // kernel READS untouched buffer
  check(n == 64, "write(pipe) from untouched buffer returned 64");
  n = read(fds[0], heap + 14*MB, 64);             // kernel WRITES untouched buffer
  check(n == 64, "read(pipe) into untouched buffer returned 64");
  close(fds[0]); close(fds[1]);
  buf = heap + 29*MB + 5*PG;
  fd = open("README", O_RDONLY);
  n = read(fd, buf, 120);                         // file data into untouched page
  close(fd);
  check(n == 120 && buf[0] == 'N' && buf[1] == 'O', "read(file) into untouched buffer got README text");
  printf(1, "   first bytes: ");
  write(1, buf, 40);
  printf(1, "...\n");
  ok = getmeminfo((struct meminfo*)(heap + 30*MB)); // copyout() into untouched struct
  check(ok == 0 && ((struct meminfo*)(heap + 30*MB))->total_pages > 0,
        "getmeminfo() filled a struct living in an untouched page");
  m1 = stats("after syscalls");
  printf(1, "   resident pages added by the kernel on our behalf: %d\n",
         m1.resident_pages - m0.resident_pages);

  // ---------------------------------------------------------------- T6
  printf(1, "\n-- T6: invalid accesses must kill only the offender --\n");
  p = heap + 32*MB + 2*PG;                         // >= sz
  pid = fork();
  if(pid == 0){ printf(1, "   child A: writing at address >= sz (0x%x)\n", p);
                *p = 1; printf(1, "   BUG: child A survived\n"); exit(); }
  wait();
  p = base - 2*PG;                                 // stack guard page (mapped, no PTE_U)
  pid = fork();
  if(pid == 0){ printf(1, "   child B: writing into the stack guard page (0x%x)\n", p);
                *p = 1; printf(1, "   BUG: child B survived\n"); exit(); }
  wait();
  pid = fork();
  if(pid == 0){ printf(1, "   child C: reading kernel address 0x80000000\n");
                n = *(volatile int*)0x80000000; printf(1, "   BUG: child C survived (%d)\n", n); exit(); }
  wait();
  printf(1, "   parent still running after three killed children\n");
  check(1, "kernel and parent survived all invalid accesses");

  // ---------------------------------------------------------------- T7
  printf(1, "\n-- T7: shrinking gives back only frames that were allocated --\n");
  m1 = stats("before sbrk(-32MB)");
  sbrk(-32 * MB);
  m2 = stats("after sbrk(-32MB)");
  printf(1, "   frames returned: %d (= resident pages dropped: %d); page-table pages stay until exit (user_pt=%d)\n",
         m2.free_pages - m1.free_pages, m1.resident_pages - m2.resident_pages,
         m2.user_pt_pages);
  check(m2.free_pages - m1.free_pages == m1.resident_pages - m2.resident_pages,
        "every freed frame was a really-allocated page");
  check(m2.user_pages == (int)((uint)base / PG), "sz is back to the pre-heap size");
  check(sbrk(-PG) == (char*)-1, "sbrk() refuses to shrink into the stack/guard page");
  pid = fork();
  if(pid == 0){ printf(1, "   child D: touching freed heap page after shrink (0x%x)\n", heap + 10*MB);
                heap[10*MB] = 1; printf(1, "   BUG: child D survived\n"); exit(); }
  wait();

  // ---------------------------------------------------------------- T8
  printf(1, "\n-- T8: out of physical memory during lazy allocation --\n");
  m0 = stats("before OOM test");
  pid = fork();
  if(pid == 0){
    char *big = sbrk(400 * MB);                    // lazily "succeeds": only sz changes
    printf(1, "   child: sbrk(400MB) returned %s; now touching every page...\n",
           big == (char*)-1 ? "-1" : "OK");
    for(i = 0; i < 400*MB; i += PG){
      big[i] = 1;
      if(i % (64*MB) == 0 && i > 0)
        printf(1, "   child: touched %d MB\n", i / MB);
    }
    printf(1, "   BUG: child touched everything\n");
    exit();
  }
  wait();
  m1 = stats("after OOM child was killed and reaped");
  check(m1.free_pages == m0.free_pages, "all frames returned after the OOM-killed child was reaped");

  printf(1, "\n==== lazytest summary: %d passed, %d failed ====\n", pass, fail);
  exit();
}
