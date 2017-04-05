#ifndef DISK_H
#define DISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A FAT filesystem disk
typedef struct disk_info *disk;

// FAT filesystem superblock information
struct __attribute__((__packed__)) superblock {
    uint32_t magic;
    uint32_t block_count;
    uint32_t fat_block_count;
    uint32_t block_size;
    uint32_t root_block;
};

// Close a FAT filesystem disk
// Returns non-zero on failure
int disk_close(disk disk);

// Format a FAT filesystem according to a superblock
// Returns non-zero on failure
int disk_format(disk disk, struct superblock sb);

// Open a FAT filesystem disk
// Returns NULL on failure
disk disk_open(const char *path, bool truncate);

// Get FAT superblock
const struct superblock *disk_superblock(const disk disk);

#endif
