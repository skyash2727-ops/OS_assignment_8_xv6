#include "types.h"
#include "stat.h"
#include "user.h"

int global_var = 42;

int main(int argc, char *argv[])
{
  int local_var = 84;

  int *heap_var = (int*) malloc(sizeof(int));
  *heap_var = 126;

  printf(1, "--- Running Virtual to Physical Address Translation Tests ---\n");

  // 1. Test Global Variable
  printf(1, "Global variable (va: 0x%x):\n", &global_var);
  int pa_global = va2pa((int)&global_var);
  if(pa_global != -1)
    printf(1, " -> Physical Address: 0x%x\n", pa_global);

  // 2. Test Local Variable
  printf(1, "Local variable (va: 0x%x):\n", &local_var);
  int pa_local = va2pa((int)&local_var);
  if(pa_local != -1)
    printf(1, " -> Physical Address: 0x%x\n", pa_local);

  // 3. Test Heap Variable (malloc)
  printf(1, "Heap variable via malloc (va: 0x%x):\n", heap_var);
  int pa_heap = va2pa((int)heap_var);
  if(pa_heap != -1)
    printf(1, " -> Physical Address: 0x%x\n", pa_heap);

  // 4. Test Unmapped/Invalid Address (0x7FFFFFFF)
  int invalid_va = 0x7FFFFFFF;
  printf(1, "Unmapped address (va: 0x%x):\n", invalid_va);
  int pa_invalid = va2pa(invalid_va);
  printf(1, " -> Result: %d (Expected: -1)\n", pa_invalid);

  // 5. Test Fork Behavior
  printf(1, "\n--- Testing fork() Address Translation ---\n");
  printf(1, "Before fork: global_var VA = 0x%x, PA = 0x%x\n", &global_var, va2pa((int)&global_var));

  int pid = fork();
  if(pid < 0) {
    printf(1, "fork failed\n");
    exit();
  } else if(pid == 0) {
    // Child Process
    printf(1, "[CHILD] global_var VA = 0x%x, PA = 0x%x\n", &global_var, va2pa((int)&global_var));
    exit();
  } else {
    // Parent Process
    wait();
    printf(1, "[PARENT] global_var VA = 0x%x, PA = 0x%x\n", &global_var, va2pa((int)&global_var));
  }

  free(heap_var);
  exit();
}