#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf("Attempting to create a filesystem snapshot...\n");
  if (snapshot() < 0) {
    printf("Snapshot creation failed.\n");
    exit(1);
  }
  printf("Filesystem snapshot created successfully.\n");
  exit(0);
}