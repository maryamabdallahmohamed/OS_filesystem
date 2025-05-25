#include "kernel/types.h"
#include "user.h"



int
main(int argc, char *argv[])
{
  printf("Taking snapshot...\n");
  snapshot();


  exit(0);
}
