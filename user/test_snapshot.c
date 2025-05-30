// test_snapshot.c - Test program for Phase 1
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fcntl.h"



// Define dirent structure for directory reading
struct dirent {
    ushort inum;
    char name[14];
};

void
test_file_operations(char *phase)
{
    printf( "\n--- Testing file operations (%s) ---\n", phase);
    
    // Create a test file
    int fd = open("testfile.txt", O_CREATE | O_WRONLY);
    if (fd < 0) {
        printf( "Failed to create testfile.txt\n");
        return;
    }
    
    char *msg = "Hello from Phase 2!\n";
    write(fd, msg, strlen(msg));
    close(fd);
    printf( "Created testfile.txt\n");
    
    // Create a directory
    if (mkdir("testdir") == 0) {
        printf( "Created testdir/\n");
    } else {
        printf( "Failed to create testdir/ (may already exist)\n");
    }
    
    // List current directory contents
    printf( "Current directory contents:\n");
    fd = open(".", O_RDONLY);
    if (fd >= 0) {
        struct dirent de;
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum != 0) {
                printf( "  %s (inum: %d)\n", de.name, de.inum);
            }
        }
        close(fd);
    }
}

int
main(int argc, char *argv[])
{
    printf( "=== Phase 2: Inode Snapshot Test ===\n");
    
    // Test initial file operations
    test_file_operations("Before Snapshot");
    
    // Create snapshot
    printf( "\n=== Creating Snapshot ===\n");
    int result = snap();
    if (result == 0) {
        printf( "Snapshot created successfully!\n");
    } else {
        printf( "Snapshot creation failed with code %d\n", result);
        exit(1);
    }
    
    // Make some changes after snapshot
    printf( "\n=== Making changes after snapshot ===\n");
    
    // Delete the test file
    if (unlink("testfile.txt") == 0) {
        printf( "Deleted testfile.txt\n");
    } else {
        printf( "Failed to delete testfile.txt\n");
    }
    
    // Create a new file
    int fd = open("newfile.txt", O_CREATE | O_WRONLY);
    if (fd >= 0) {
        char *msg = "This file was created after snapshot\n";
        write(fd, msg, strlen(msg));
        close(fd);
        printf( "Created newfile.txt\n");
    }
    
    // Show current state
    test_file_operations("After Changes");
    
    // Restore snapshot
    printf( "\n=== Restoring Snapshot ===\n");
    result = restore();
    if (result == 0) {
        printf( "Snapshot restored successfully!\n");
    } else {
        printf( "Snapshot restoration failed with code %d\n", result);
    }
    
    // Test file operations after restore
    test_file_operations("After Restore");
    
    printf( "\n=== Phase 2 Test Completed ===\n");
    printf( "Check if:\n");
    printf( "1. testfile.txt is back\n");
    printf( "2. newfile.txt is gone\n");
    printf( "3. Directory structure matches snapshot\n");
    
    exit(0);
}