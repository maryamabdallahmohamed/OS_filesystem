// snapshot.c - Phase 2, 3 & 4: Complete Filesystem Snapshotting
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "stat.h"  // Add this at the top of snapshot.c
#include "fs.h"
#include "buf.h"

// Complete snapshot structure for Phase 2, 3 & 4
struct snapshot {
    int valid;              // Is this snapshot valid?
    uint nblocks;          // Number of blocks in filesystem
    uint ninodes;          // Number of inodes
    uint nlog;             // Number of log blocks
    uint logstart;         // Block number of first log block
    uint inodestart;       // Block number of first inode block
    uint bmapstart;        // Block number of first free map block
    
    // Phase 2: Inode table storage
    struct dinode *inode_backup;  // Backup of all inodes
    uint inode_blocks;            // Number of inode blocks
    
    // Phase 3: Directory data storage
    char *dir_data_backup;        // Backup of directory data blocks
    uint dir_data_size;           // Size of directory data backup
    uint *dir_block_map;          // Map of which blocks contain directory data
    uint dir_block_count;         // Number of directory blocks
    
    // Phase 4: File data blocks and bitmap
    char *file_data_backup;       // Backup of file content blocks
    uint file_data_size;          // Size of file data backup
    uint *file_block_map;         // Map of which blocks contain file data
    uint file_block_count;        // Number of file data blocks
    char *bitmap_backup;          // Backup of free block bitmap
    uint bitmap_blocks;           // Number of bitmap blocks
    
    char label[32];        // Snapshot label
};

static struct snapshot current_snapshot;

void
snapshot_init(void)
{
    current_snapshot.valid = 0;
    current_snapshot.inode_backup = 0;
    current_snapshot.dir_data_backup = 0;
    current_snapshot.dir_block_map = 0;
    current_snapshot.file_data_backup = 0;
    current_snapshot.file_block_map = 0;
    current_snapshot.bitmap_backup = 0;
    printf("Snapshot system initialized\n");
}

// Helper function: Read superblock information
static void
read_superblock_info(struct superblock *sb)
{
    struct buf *bp = bread(ROOTDEV, 1);  // Superblock is at block 1
    memmove(sb, bp->data, sizeof(*sb));
    brelse(bp);
}

// Helper function: Calculate number of inode blocks
uint
calc_inode_blocks(uint ninodes)
{
    return (ninodes * sizeof(struct dinode) + BSIZE - 1) / BSIZE;
}

// Helper function: Calculate number of bitmap blocks
static uint
calc_bitmap_blocks(uint nblocks)
{
    return (nblocks + BPB - 1) / BPB;  // BPB = bits per block
}

// Helper function to invalidate inode cache
static void
invalidate_inode_cache(void)
{
    printf("Invalidating inode cache\n");
    // Simple approach - let xv6 naturally refresh inodes as needed
}

// Phase 2: Save inode table
static int
save_inode_table(struct superblock *sb)
{
    uint inode_blocks = calc_inode_blocks(sb->ninodes);
    
    printf("Saving inode table: %d inodes in %d blocks\n", 
           sb->ninodes, inode_blocks);
    
    current_snapshot.inode_backup = kalloc();
    if (!current_snapshot.inode_backup) {
        printf("Failed to allocate memory for inode backup\n");
        return -1;
    }
    
    struct buf *bp;
    char *backup_ptr = (char*)current_snapshot.inode_backup;
    
    for (uint b = 0; b < inode_blocks && b * BSIZE < PGSIZE; b++) {
        bp = bread(ROOTDEV, sb->inodestart + b);
        
        uint copy_size = BSIZE;
        if ((b + 1) * BSIZE > PGSIZE) {
            copy_size = PGSIZE - (b * BSIZE);
        }
        
        memmove(backup_ptr + (b * BSIZE), bp->data, copy_size);
        brelse(bp);
        
        if ((b + 1) * BSIZE >= PGSIZE) {
            printf("Warning: Inode table too large, truncating backup\n");
            break;
        }
    }
    
    current_snapshot.inode_blocks = inode_blocks;
    printf("Inode table saved successfully\n");
    return 0;
}

// Phase 3: Save directory data blocks
static int
save_directory_data(struct superblock *sb)
{
    printf("Phase 3: Saving directory data blocks\n");
    
    current_snapshot.dir_block_map = kalloc();
    if (!current_snapshot.dir_block_map) {
        printf("Failed to allocate directory block map\n");
        return -1;
    }
    
    current_snapshot.dir_data_backup = kalloc();
    if (!current_snapshot.dir_data_backup) {
        printf("Failed to allocate directory data backup\n");
        kfree((char*)current_snapshot.dir_block_map);
        return -1;
    }
    
    uint dir_blocks_found = 0;
    uint data_offset = 0;
    uint *block_map = (uint*)current_snapshot.dir_block_map;
    
    struct buf *inode_bp;
    for (uint inode_block = 0; inode_block < current_snapshot.inode_blocks; inode_block++) {
        inode_bp = bread(ROOTDEV, sb->inodestart + inode_block);
        struct dinode *dinodes = (struct dinode*)inode_bp->data;
        
        uint inodes_per_block = BSIZE / sizeof(struct dinode);
        for (uint i = 0; i < inodes_per_block; i++) {
            struct dinode *di = &dinodes[i];
            
            if (di->type == T_DIR && di->size > 0) {
                printf("Found directory inode %d, size %d\n", 
                       inode_block * inodes_per_block + i, di->size);
                
                for (int j = 0; j < NDIRECT && di->addrs[j] != 0; j++) {
                    if (dir_blocks_found >= PGSIZE/sizeof(uint)) {
                        printf("Too many directory blocks, truncating\n");
                        break;
                    }
                    
                    uint block_addr = di->addrs[j];
                    block_map[dir_blocks_found] = block_addr;
                    
                    struct buf *data_bp = bread(ROOTDEV, block_addr);
                    if (data_offset + BSIZE <= PGSIZE) {
                        memmove(current_snapshot.dir_data_backup + data_offset, 
                               data_bp->data, BSIZE);
                        data_offset += BSIZE;
                        dir_blocks_found++;
                        printf("  Backed up directory block %d\n", block_addr);
                    }
                    brelse(data_bp);
                    
                    if (data_offset + BSIZE > PGSIZE) {
                        printf("Directory data backup full\n");
                        break;
                    }
                }
            }
        }
        brelse(inode_bp);
        
        if (data_offset + BSIZE > PGSIZE) break;
    }
    
    current_snapshot.dir_block_count = dir_blocks_found;
    current_snapshot.dir_data_size = data_offset;
    
    printf("Saved %d directory blocks (%d bytes)\n", 
           dir_blocks_found, data_offset);
    return 0;
}

// Phase 4: Save file data blocks
static int
save_file_data(struct superblock *sb)
{
    printf("Phase 4: Saving file data blocks\n");
    
    current_snapshot.file_block_map = kalloc();
    if (!current_snapshot.file_block_map) {
        printf("Failed to allocate file block map\n");
        return -1;
    }
    
    current_snapshot.file_data_backup = kalloc();
    if (!current_snapshot.file_data_backup) {
        printf("Failed to allocate file data backup\n");
        kfree((char*)current_snapshot.file_block_map);
        return -1;
    }
    
    uint file_blocks_found = 0;
    uint data_offset = 0;
    uint *block_map = (uint*)current_snapshot.file_block_map;
    
    struct buf *inode_bp;
    for (uint inode_block = 0; inode_block < current_snapshot.inode_blocks; inode_block++) {
        inode_bp = bread(ROOTDEV, sb->inodestart + inode_block);
        struct dinode *dinodes = (struct dinode*)inode_bp->data;
        
        uint inodes_per_block = BSIZE / sizeof(struct dinode);
        for (uint i = 0; i < inodes_per_block; i++) {
            struct dinode *di = &dinodes[i];
            
            // Look for regular files with data
            if (di->type == T_FILE && di->size > 0) {
                printf("Found file inode %d, size %d\n", 
                       inode_block * inodes_per_block + i, di->size);
                
                // Save file's direct blocks
                for (int j = 0; j < NDIRECT && di->addrs[j] != 0; j++) {
                    if (file_blocks_found >= PGSIZE/sizeof(uint)) {
                        printf("Too many file blocks, truncating\n");
                        break;
                    }
                    
                    uint block_addr = di->addrs[j];
                    block_map[file_blocks_found] = block_addr;
                    
                    struct buf *data_bp = bread(ROOTDEV, block_addr);
                    if (data_offset + BSIZE <= PGSIZE) {
                        memmove(current_snapshot.file_data_backup + data_offset, 
                               data_bp->data, BSIZE);
                        data_offset += BSIZE;
                        file_blocks_found++;
                        printf("  Backed up file block %d\n", block_addr);
                    }
                    brelse(data_bp);
                    
                    if (data_offset + BSIZE > PGSIZE) {
                        printf("File data backup full\n");
                        break;
                    }
                }
                
                // Handle indirect block if present
                if (di->addrs[NDIRECT] != 0 && data_offset + BSIZE <= PGSIZE) {
                    printf("  Found indirect block %d\n", di->addrs[NDIRECT]);
                    
                    struct buf *indirect_bp = bread(ROOTDEV, di->addrs[NDIRECT]);
                    uint *indirect_addrs = (uint*)indirect_bp->data;
                    
                    for (uint k = 0; k < NINDIRECT && indirect_addrs[k] != 0; k++) {
                        if (file_blocks_found >= PGSIZE/sizeof(uint) || 
                            data_offset + BSIZE > PGSIZE) {
                            break;
                        }
                        
                        uint block_addr = indirect_addrs[k];
                        block_map[file_blocks_found] = block_addr;
                        
                        struct buf *data_bp = bread(ROOTDEV, block_addr);
                        memmove(current_snapshot.file_data_backup + data_offset, 
                               data_bp->data, BSIZE);
                        data_offset += BSIZE;
                        file_blocks_found++;
                        printf("    Backed up indirect file block %d\n", block_addr);
                        brelse(data_bp);
                    }
                    brelse(indirect_bp);
                }
            }
        }
        brelse(inode_bp);
        
        if (data_offset + BSIZE > PGSIZE) break;
    }
    
    current_snapshot.file_block_count = file_blocks_found;
    current_snapshot.file_data_size = data_offset;
    
    printf("Saved %d file blocks (%d bytes)\n", 
           file_blocks_found, data_offset);
    return 0;
}

// Phase 4: Save free block bitmap
static int
save_bitmap(struct superblock *sb)
{
    printf("Phase 4: Saving free block bitmap\n");
    
    uint bitmap_blocks = calc_bitmap_blocks(sb->nblocks);
    current_snapshot.bitmap_blocks = bitmap_blocks;
    
    current_snapshot.bitmap_backup = kalloc();
    if (!current_snapshot.bitmap_backup) {
        printf("Failed to allocate bitmap backup\n");
        return -1;
    }
    
    struct buf *bp;
    char *backup_ptr = current_snapshot.bitmap_backup;
    
    for (uint b = 0; b < bitmap_blocks && b * BSIZE < PGSIZE; b++) {
        bp = bread(ROOTDEV, sb->bmapstart + b);
        
        uint copy_size = BSIZE;
        if ((b + 1) * BSIZE > PGSIZE) {
            copy_size = PGSIZE - (b * BSIZE);
        }
        
        memmove(backup_ptr + (b * BSIZE), bp->data, copy_size);
        brelse(bp);
        
        if ((b + 1) * BSIZE >= PGSIZE) {
            printf("Warning: Bitmap too large, truncating backup\n");
            break;
        }
    }
    
    printf("Bitmap saved successfully (%d blocks)\n", bitmap_blocks);
    return 0;
}

// Phase 2: Restore inode table
static int
restore_inode_table(void)
{
    if (!current_snapshot.inode_backup) {
        printf("No inode backup to restore\n");
        return -1;
    }
    
    printf("Restoring inode table: %d blocks\n", current_snapshot.inode_blocks);
    
    struct buf *bp;
    char *backup_ptr = (char*)current_snapshot.inode_backup;
    
    for (uint b = 0; b < current_snapshot.inode_blocks && b * BSIZE < PGSIZE; b++) {
        bp = bread(ROOTDEV, current_snapshot.inodestart + b);
        
        uint copy_size = BSIZE;
        if ((b + 1) * BSIZE > PGSIZE) {
            copy_size = PGSIZE - (b * BSIZE);
        }
        
        memmove(bp->data, backup_ptr + (b * BSIZE), copy_size);
        bwrite(bp);
        brelse(bp);
        
        if ((b + 1) * BSIZE >= PGSIZE) {
            break;
        }
    }
    
    printf("Inode table restored successfully\n");
    return 0;
}

// Phase 3: Restore directory data blocks
static int
restore_directory_data(void)
{
    if (!current_snapshot.dir_data_backup || !current_snapshot.dir_block_map) {
        printf("No directory data backup to restore\n");
        return -1;
    }
    
    printf("Restoring %d directory blocks\n", current_snapshot.dir_block_count);
    
    uint *block_map = (uint*)current_snapshot.dir_block_map;
    uint data_offset = 0;
    
    for (uint i = 0; i < current_snapshot.dir_block_count; i++) {
        uint block_addr = block_map[i];
        
        struct buf *bp = bread(ROOTDEV, block_addr);
        memmove(bp->data, current_snapshot.dir_data_backup + data_offset, BSIZE);
        bwrite(bp);
        brelse(bp);
        
        data_offset += BSIZE;
        printf("  Restored directory block %d\n", block_addr);
    }
    
    printf("Directory data restored successfully\n");
    return 0;
}

// Phase 4: Restore file data blocks
static int
restore_file_data(void)
{
    if (!current_snapshot.file_data_backup || !current_snapshot.file_block_map) {
        printf("No file data backup to restore\n");
        return -1;
    }
    
    printf("Restoring %d file blocks\n", current_snapshot.file_block_count);
    
    uint *block_map = (uint*)current_snapshot.file_block_map;
    uint data_offset = 0;
    
    for (uint i = 0; i < current_snapshot.file_block_count; i++) {
        uint block_addr = block_map[i];
        
        struct buf *bp = bread(ROOTDEV, block_addr);
        memmove(bp->data, current_snapshot.file_data_backup + data_offset, BSIZE);
        bwrite(bp);
        brelse(bp);
        
        data_offset += BSIZE;
        printf("  Restored file block %d\n", block_addr);
    }
    
    printf("File data restored successfully\n");
    return 0;
}

// Phase 4: Restore free block bitmap
static int
restore_bitmap(void)
{
    if (!current_snapshot.bitmap_backup) {
        printf("No bitmap backup to restore\n");
        return -1;
    }
    
    printf("Restoring bitmap: %d blocks\n", current_snapshot.bitmap_blocks);
    
    struct buf *bp;
    char *backup_ptr = current_snapshot.bitmap_backup;
    
    for (uint b = 0; b < current_snapshot.bitmap_blocks && b * BSIZE < PGSIZE; b++) {
        bp = bread(ROOTDEV, current_snapshot.bmapstart + b);
        
        uint copy_size = BSIZE;
        if ((b + 1) * BSIZE > PGSIZE) {
            copy_size = PGSIZE - (b * BSIZE);
        }
        
        memmove(bp->data, backup_ptr + (b * BSIZE), copy_size);
        bwrite(bp);
        brelse(bp);
        
        if ((b + 1) * BSIZE >= PGSIZE) {
            break;
        }
    }
    
    printf("Bitmap restored successfully\n");
    return 0;
}

int
sys_snap(void)
{
    printf("=== Creating Complete Filesystem Snapshot (Phase 2-4) ===\n");
    
    // Clean up previous snapshot if exists
    if (current_snapshot.valid) {
        if (current_snapshot.inode_backup) {
            kfree((char*)current_snapshot.inode_backup);
            current_snapshot.inode_backup = 0;
        }
        if (current_snapshot.dir_data_backup) {
            kfree(current_snapshot.dir_data_backup);
            current_snapshot.dir_data_backup = 0;
        }
        if (current_snapshot.dir_block_map) {
            kfree((char*)current_snapshot.dir_block_map);
            current_snapshot.dir_block_map = 0;
        }
        if (current_snapshot.file_data_backup) {
            kfree(current_snapshot.file_data_backup);
            current_snapshot.file_data_backup = 0;
        }
        if (current_snapshot.file_block_map) {
            kfree((char*)current_snapshot.file_block_map);
            current_snapshot.file_block_map = 0;
        }
        if (current_snapshot.bitmap_backup) {
            kfree(current_snapshot.bitmap_backup);
            current_snapshot.bitmap_backup = 0;
        }
    }
    
    // Read superblock information
    struct superblock sb;
    read_superblock_info(&sb);
    
    // Store superblock info in snapshot
    current_snapshot.nblocks = sb.nblocks;
    current_snapshot.ninodes = sb.ninodes;
    current_snapshot.nlog = sb.nlog;
    current_snapshot.logstart = sb.logstart;
    current_snapshot.inodestart = sb.inodestart;
    current_snapshot.bmapstart = sb.bmapstart;
    
    printf("Filesystem info: %d blocks, %d inodes\n", sb.nblocks, sb.ninodes);
    printf("Inode start: %d, Bitmap start: %d\n", sb.inodestart, sb.bmapstart);
    
    // Phase 2: Save inode table
    if (save_inode_table(&sb) < 0) {
        printf("Failed to save inode table\n");
        return -1;
    }
    
    // Phase 3: Save directory data
    if (save_directory_data(&sb) < 0) {
        printf("Failed to save directory data\n");
        return -1;
    }
    
    // Phase 4: Save file data blocks
    if (save_file_data(&sb) < 0) {
        printf("Failed to save file data\n");
        return -1;
    }
    
    // Phase 4: Save bitmap
    if (save_bitmap(&sb) < 0) {
        printf("Failed to save bitmap\n");
        return -1;
    }
    
    // Mark snapshot as valid
    current_snapshot.valid = 1;
    strncpy(current_snapshot.label, "Complete_Snapshot", 31);
    current_snapshot.label[31] = '\0';
    
    printf("Complete snapshot '%s' created successfully!\n", current_snapshot.label);
    return 0;
}

int
sys_restore(void)
{
    printf("=== Restoring Complete Filesystem Snapshot (Phase 2-4) ===\n");
    
    if (!current_snapshot.valid) {
        printf("No valid snapshot to restore\n");
        return -1;
    }
    
    printf("Restoring snapshot '%s'\n", current_snapshot.label);
    printf("Original filesystem: %d blocks, %d inodes\n",
           current_snapshot.nblocks, current_snapshot.ninodes);
    
    // Phase 4: Restore bitmap first (free block management)
    if (restore_bitmap() < 0) {
        printf("Failed to restore bitmap\n");
        return -1;
    }
    
    // Phase 2: Restore inode table
    if (restore_inode_table() < 0) {
        printf("Failed to restore inode table\n");
        return -1;
    }
    
    // Phase 3: Restore directory data
    if (restore_directory_data() < 0) {
        printf("Failed to restore directory data\n");
        return -1;
    }
    
    // Phase 4: Restore file data
    if (restore_file_data() < 0) {
        printf("Failed to restore file data\n");
        return -1;
    }
    
    // Invalidate cache after restoration
    invalidate_inode_cache();
    
    printf("Complete snapshot restored successfully!\n");
    printf("All filesystem components have been restored:\n");
    printf("- Inodes and metadata\n");
    printf("- Directory structure and entries\n");
    printf("- File contents and data blocks\n");
    printf("- Free block bitmap\n");
    
    return 0;
}

// Helper function to display snapshot info (for debugging)
void
snapshot_info(void)
{
    if (!current_snapshot.valid) {
        printf("No valid snapshot exists\n");
        return;
    }
    
    printf("=== Complete Snapshot Information ===\n");
    printf("Label: %s\n", current_snapshot.label);
    printf("Blocks: %d, Inodes: %d\n", 
           current_snapshot.nblocks, current_snapshot.ninodes);
    printf("Inode blocks backed up: %d\n", current_snapshot.inode_blocks);
    printf("Directory blocks backed up: %d (%d bytes)\n", 
           current_snapshot.dir_block_count, current_snapshot.dir_data_size);
    printf("File blocks backed up: %d (%d bytes)\n", 
           current_snapshot.file_block_count, current_snapshot.file_data_size);
    printf("Bitmap blocks backed up: %d\n", current_snapshot.bitmap_blocks);
    printf("Memory allocated: Inodes=%s, Dirs=%s, Files=%s, Bitmap=%s\n",
           current_snapshot.inode_backup ? "Yes" : "No",
           current_snapshot.dir_data_backup ? "Yes" : "No",
           current_snapshot.file_data_backup ? "Yes" : "No",
           current_snapshot.bitmap_backup ? "Yes" : "No");
}