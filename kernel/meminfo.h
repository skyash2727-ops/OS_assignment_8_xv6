// meminfo.h -- shared between kernel (sysproc.c) and user programs.
// Filled in by the getmeminfo() system call (Assignment 8, Q2).
struct meminfo {
  int total_pages;      // physical pages handed to the allocator at boot
  int free_pages;       // pages currently on kmem.freelist
  int user_pages;       // virtual pages in user address space: PGROUNDUP(sz)/PGSIZE
  int pt_pages;         // pages used by this process's page tables
                        //   = 1 (page directory) + user_pt_pages + kernel_pt_pages
  int user_pt_pages;    //   page-table pages that map user VA  (PDE index < 512)
  int kernel_pt_pages;  //   page-table pages that map kernel VA (PDE index >= 512)
  int resident_pages;   // user pages that really have a frame (PTE_P set, VA < sz)
};
