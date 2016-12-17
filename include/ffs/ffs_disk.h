#ifndef FFS_DISK_H
#define FFS_DISK_H

#include <stddef.h>
#include <stdint.h>

// A FAT filesystem disk
typedef struct ffs_disk_info *ffs_disk;

// FAT filesystem superblock information
struct __attribute__((__packed__)) ffs_superblock {
    uint32_t magic;
    uint32_t block_count;
    uint32_t fat_block_count;
    uint32_t block_size;
    int32_t root_block;
};

// Close a FAT filesystem disk
// Returns zero on success; otherwise, returns non-zero
int ffs_disk_close(ffs_disk disk);

// Initialize a FAT filesystem on specifed disk
// Returns zero on success; otherwise, returns non-zero
int ffs_disk_init(ffs_disk disk, size_t block_count);

// Open a FAT filesystem disk with name pointed to by path
// Returns disk on success; otherwise, returns NULL
ffs_disk ffs_disk_open(const char *path);

// Get FAT filesystem superblock for specified disk
// Returns superblock on success; otherwise, returns NULL
const struct ffs_superblock *ffs_disk_superblock(const ffs_disk disk);

#endif
