#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE (1 << 12)
#define PAGES 4
#define SHM_PARENT_BASE_ADDR 0x60000000
#define SHM_CHILD_BASE_ADDR 0x65000000
#define MEM_PATERN 0xAAAAAAAA

int
main(void)
{
  uint token = 0x3A3A3A3A;
  printf(1, "[Parent] pid: '%d'\n", getpid());

  printf(1, "[Parent] Requesting shared memory:\ntoken: '%x', size: '%d' bytes, \
mapped at '%x'\n",
    token, PAGES*PGSIZE, SHM_PARENT_BASE_ADDR);

  if (shmget(token, (char*)SHM_PARENT_BASE_ADDR,
      PAGES*PGSIZE) < 0) {
    printf(2, "shmget failed!\n");
    exit();
  }

  printf(1, "[Parent] Writing '0x%x' pattern to shared memory, mapped at '0x%x'\n", 
    MEM_PATERN, SHM_PARENT_BASE_ADDR);
  uint* pt = (uint*)SHM_PARENT_BASE_ADDR;
  for (; pt < ((uint*)SHM_PARENT_BASE_ADDR) + PAGES*(PGSIZE/sizeof(uint)); pt++) {   
    *pt = MEM_PATERN;
  }

  printf(1, "[Parent] Forked a child process\n");
  int pid = fork();

  if (pid < 0) {
    exit();
  }
  else if(!pid) { // child
    printf(1, "[Child] pid: '%d'\n", getpid());

  printf(1, "[Child] Requesting shared memory:\ntoken: '%x', \
mapped at '%x'\n",
    token, SHM_CHILD_BASE_ADDR);
    if (shmget(token, (char*)SHM_CHILD_BASE_ADDR, 0) < 0) {
      printf(2, "shmget failed!\n");
      exit();
    }

    printf(1, "[Child] Reading '0x%x' pattern from shared memory, mapped at '0x%x'\n",
      MEM_PATERN, SHM_CHILD_BASE_ADDR);

    uint* pt = (uint*)SHM_CHILD_BASE_ADDR;
    for (; pt < ((uint*)SHM_CHILD_BASE_ADDR) + PAGES*(PGSIZE/sizeof(uint)); pt++)
      // printf("%x", *pt); // [DEBUG]
      if(*pt != MEM_PATERN) {
        printf(2, "Invalid pattern at '%p', expected '%x', found '%x'!\n", 
          pt, MEM_PATERN, *pt);
        exit();
      }
    //printf("\n") // [DEBUG]
    printf(1, "Success!\n");
  }

  wait();
  exit();
}