#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"  // ✅ Add this for struct sleeplock
#include "fs.h"
#include "file.h"
#include "buf.h"
#include "proc.h"

#define SNAPSHOT_START 800  // block number to write snapshot
#define SNAPSHOT_BLOCKS 100 // number of blocks to copy

void snapshot(void) {
  struct buf *src, *dst;

  for (int i = 0; i < SNAPSHOT_BLOCKS; i++) {
    src = bread(ROOTDEV, i);                  // read original fs block
    dst = bread(ROOTDEV, SNAPSHOT_START + i); // read destination block

    memmove(dst->data, src->data, BSIZE);     // copy block data

    bwrite(dst);  // write back snapshot
    brelse(src);
    brelse(dst);
  }

  return ;  // return dummy snapshot ID
}

void restore(void) {
  struct buf *src, *dst;
  printf("Restore Called......s");

  // For now, ignore snapshot_id (we only support one snapshot)
  for (int i = 0; i < SNAPSHOT_BLOCKS; i++) {
    src = bread(ROOTDEV, SNAPSHOT_START + i); // read from snapshot area
    dst = bread(ROOTDEV, i);                  // read original FS block

    memmove(dst->data, src->data, BSIZE);     // overwrite live FS block

    bwrite(dst);  // write changes
    brelse(src);
    brelse(dst);
  }

  return ;  // success
}
