#include "kernel/types.h"
#include "user.h"


int
main(void)
{
  printf("Restoring snapshot...\n");
  restore(); // the syscall
  exit(0);
}
