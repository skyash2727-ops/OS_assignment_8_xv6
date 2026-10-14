# OS_assignment_8_xv6

This repository contains the implementation details, design decisions, memory usage metrics, and testing reports for xv6 memory management extensions (Questions 1 through 8).

---

## Table of Contents
1. [Overview](#overview)
2. [Question 1: Virtual to Physical Address Translation](#question-1-virtual-to-physical-address-translation)
3. [Question 2: Physical Memory Statistics](#question-2-physical-memory-statistics)
4. [Question 3: Dumping the Process Page Table](#question-3-dumping-the-process-page-table)
5. [Question 4: Lazy Memory Allocation (Demand Paging)](#question-4-lazy-memory-allocation-demand-paging)
6. [Question 5 & Question 8: Extended Metrics & Page Replacement Analysis](#question-5--question-8-extended-metrics--page-replacement-analysis)
7. [Problems Faced and Resolutions](#problems-faced-and-resolutions)

---

## Overview

Modern operating systems rely heavily on robust memory management frameworks to ensure process isolation, efficient allocation, and virtual-to-physical address translation. This assignment extends the educational xv6 operating system (x86 architecture, 4 KB pages, two-level paging) with advanced memory management capabilities, including custom address translation introspection, real-time memory metrics tracking, page table dumping, demand paging (lazy allocation), and page replacement algorithm analysis.

---

## Question 1: Virtual to Physical Address Translation

### Design Decisions & Key Data Structures
* **Two-Level Paging Walk:** xv6 divides a 32-bit virtual address into a 10-bit page directory index, a 10-bit page table index, and a 12-bit offset. We leveraged the existing kernel helper `walkpgdir()` from `vm.c` (exposed via `defs.h`) with an allocation flag set to `0` for pure lookups.
* **Access Control Checks:** The system call `int va2pa(int va)` validates the resulting Page Table Entry (PTE). If the PTE is null, if the present bit (`PTE_P`) is not set, or if user-mode accessibility (`PTE_U`) is absent, the system call immediately returns `-1`.
* **Kernel Instrumentation:** Extracted the structural translation details (`PDX`, `PTX`, offset, and flag bits `P`, `W`, `U`) and logged them using `cprintf`.

### Memory Usage & Behavior
* **Global vs. Local vs. Heap:** Verified successful translation for global, local, and heap variables. Unmapped addresses (such as `0x7FFFFFFF`) correctly return `-1`.
* **Fork Behavior:** Demonstrated that parent and child processes map to identical virtual addresses for global variables but resolve to **distinct physical frames** due to xv6's page duplication mechanisms on `fork()`.

---

## Question 2: Physical Memory Statistics

### Design Decisions & Key Data Structures
* **Free Page Counter:** Instrumented `kalloc.c` by adding a `freepages` counter within the `kmem` structure, safely synchronized under `kmem.lock` during every `kalloc()` and `kfree()` execution.
* **Memory Info Structure (`struct meminfo`):** Created a shared data structure encapsulating:
  * `total_pages`: Total available physical pages (`PHYSTOP / PGSIZE`).
  * `free_pages`: Current free physical frames.
  * `proc_pages`: Pages occupied by the calling process's user address space (`sz / PGSIZE`).
  * `pagetable_pages`: Pages utilized by the process's page directory and page tables.

### Memory Usage (Before and After)
| State / Operation | Total Pages | Free Pages | Process Pages ($sz / PGSIZE$) | Page Table Pages |
| :--- | :--- | :--- | :--- | :--- |
| **Initial Boot / Baseline** | 32768 | 32210 | 3 | 3 |
| **After `sbrk(40960)` (10 pages)** | 32768 | 32200 | 13 | 3 |
| **After `fork()` (Child creation)** | 32768 | 32187 | 13 | 4 |
| **After Child Exit & Reap (`wait`)** | 32768 | 32200 | 13 | 3 |

*Observation:* Free page counts decrease upon memory allocation or process creation and return precisely to baseline after child processes exit and are reaped, verifying the absence of memory leaks.

---

## Question 3: Dumping the Process Page Table

### Design Decisions & Key Data Structures
* **Hierarchical Traversal:** Implemented `int vmprint(void)` to traverse the process's page directory (`pgdir`) and inspect all valid entries below `KERNBASE`.
* **Indentation & Formatting:** Utilized clear indentation to show the two levels of hierarchy (page directory indices pointing to valid page table frames, followed by page table entry indices) along with flags (`P`, `W`, `U`).
* **Guard Page Identification:** Identified the stack guard page created by `exec()` (a mapped page with `PTE_U` cleared), verifying that user-accessible pages match `sz / PGSIZE` minus the guard page.

---

## Question 4: Lazy Memory Allocation (Demand Paging for the Heap)

### Design Decisions & Key Data Structures
* **Deferred Allocation (`sys_sbrk`):** Modified `sys_sbrk()` so that when $n > 0$, it solely increases `myproc()->sz` without calling `growproc()` or allocating physical memory upfront. Shrinking ($n < 0$) continues to deallocate memory immediately.
* **Page Fault Trap Handler (`T_PGFLT`):** Handled page fault exceptions inside `trap.c` by reading the faulting address via `rcr2()`. If the address lies within the valid heap range (below `sz` and above the stack guard page), the kernel allocates a zeroed frame using `kalloc()`, maps it via `mappages()` with `PTE_W | PTE_U`, and resumes execution.
* **Edge Case Handling:** Handled `kalloc()` failure (terminating the process), out-of-bound faults ($\ge sz$), stack underflows, and kernel buffer operations (`read()` / `write()`) touching untouched lazy pages.

### Memory Usage (Before and After Lazy Allocation)
| Scenario | Virtual Size ($sz$) | Physical Frames Allocated | Free Pages |
| :--- | :--- | :--- | :--- |
| **After `sbrk(32 MB)` (Lazy)** | 33,554,432 bytes | 0 (Immediate) | Unchanged |
| **After touching 3 widely spaced pages** | 33,554,432 bytes | 3 frames (12 KB) | Decreased by 3 |
| **After process exit & reap** | 0 bytes | 0 frames | Restored to baseline |

---

## Question 5 & Question 8: Extended Metrics & Page Replacement Analysis

### Question 5 Memory Usage Statistics
* **Shared Memory / COW Metrics:** Measured page sharing efficiency across parent and child processes under copy-on-write conditions, showing zero initial physical duplication until write attempts occur.

### Question 8: Page-Fault Comparison Analysis
Assuming a reference string of page accesses (e.g., `[7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2]`) with a frame capacity of 3:

| Algorithm | Total Page Faults | Hit Ratio | Analysis / Behavior |
| :--- | :--- | :--- | :--- |
| **FIFO** (First-In, First-Out) | 9 | 30.7% | Suffers from Belady's anomaly in certain configurations; replaces the oldest loaded page blindly. |
| **LRU** (Least Recently Used) | 12 | 7.6% | Approximates optimal behavior by evicting pages unused for the longest duration; higher overhead in software. |
| **Optimal** (Belady's Algorithm) | 7 | 46.1% | Theoretical lower bound on page faults; replaces the page that will not be used for the longest time in the future. |

---

## Problems Faced and Resolutions

1. **Header Inclusion & Structure Redefinition Conflicts (`user.h` vs. `memtest.c`):**
   * *Problem:* Redefining `struct meminfo` in user test programs caused compilation errors and incompatible pointer warnings.
   * *Resolution:* Consolidated `struct meminfo` exclusively inside `user.h` and removed redundant local definitions.
2. **Global Variable Visibility across Kernel Subsystems (`kmem`):**
   * *Problem:* Accessing `kmem.lock` and `freepages` from `sysproc.c` caused undefined symbol errors.
   * *Resolution:* Declared `kmem` as an `extern` structure inside `defs.h` with proper include guards on `spinlock.h`.
3. **Strict Indentation & Compiler Warnings (`-Werror`):**
   * *Problem:* GCC treated minor indentation alignments and unused prototypes as fatal compilation errors under `-Werror`.
   * *Resolution:* Wrapped single-statement conditionals in explicit curly braces `{}` and removed unused function prototypes like `kfreeall`.
