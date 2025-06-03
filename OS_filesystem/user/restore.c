#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf("Attempting to restore the filesystem snapshot...\n");
  if (restore() < 0) {
    printf("Snapshot restoration failed.\n");
    exit(1);
  }
  printf("Filesystem snapshot restored successfully.\n");
  exit(0);
}