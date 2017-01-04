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
    uint32_t root_block;
};

// Close a FAT filesystem disk
// Returns non-zero on failure
int ffs_disk_close(ffs_disk disk);

// Format a FAT filesystem according to a superblock
// Returns non-zero on failure
int ffs_disk_format(ffs_disk disk, struct ffs_superblock sb);

// Open a FAT filesystem disk
// Returns NULL on failure
ffs_disk ffs_disk_open(const char *path);

// Get FAT superblock
const struct ffs_superblock *ffs_disk_superblock(const ffs_disk disk);

#endif
