#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>
#include <ffs/ffs_disk.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FFS_DISK_BLOCK_SIZE	1024

struct ffs_disk_info {
    FILE *file;
    struct ffs_superblock superblock;
};

// Must be defined here because ffs_disk is defined here
int ffs_block_read(ffs_disk disk, int block, void *buffer)
{
	if(block == FFS_BLOCK_LAST || block == FFS_BLOCK_INVALID) {
		return -1;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
        return -1;
    }

	// Seek to block position
	if(fseek(disk->file, block * superblock->block_size, SEEK_SET) != 0) {
		return -1;
	}

	// Read entire block
	if(fread(buffer, superblock->block_size, 1, disk->file) != 1) {
		return -1;
	}

	return 0;
}

// Must be defined here because ffs_disk is defined here
int ffs_block_write(ffs_disk disk, int block, const void *buffer)
{
	if(block == FFS_BLOCK_LAST || block == FFS_BLOCK_INVALID) {
		return -1;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
        return -1;
    }

	// Seek to block position
	if(fseek(disk->file, block * superblock->block_size, SEEK_SET) != 0) {
		return -1;
	}

	// Read entire block
	if(fwrite(buffer, superblock->block_size, 1, disk->file) != 1) {
		return -1;
	}

	return 0;
}

int ffs_disk_close(ffs_disk disk)
{
    // Disk must not be NULL
    if(!disk) {
        return -1;
    }

    // Must be able to close disk file
    if(fclose(disk->file) != 0) {
        return -1;
    }

    // Done using disk
    free(disk);

    return 0;
}

int ffs_disk_init(ffs_disk disk, size_t block_count)
{
    const size_t fat_size = block_count * sizeof(int32_t);

    // Setup disk superblock
    disk->superblock.magic = 0x2345beef;
    disk->superblock.block_count = block_count;
    disk->superblock.block_size = FFS_DISK_BLOCK_SIZE;
    disk->superblock.fat_block_count = fat_size / disk->superblock.block_size + (fat_size % disk->superblock.block_size > 0);

    // Need a buffer to store a block
    struct ffs_superblock *superblock_buffer = malloc(FFS_DISK_BLOCK_SIZE);
    if(!superblock_buffer) {
        return -1;
    }

    *superblock_buffer = disk->superblock;

    // Write superblock to be read when the disk is opened
    if(ffs_block_write(disk, FFS_BLOCK_SUPERBLOCK, superblock_buffer) != 0) {
		free(superblock_buffer);
        return -1;
    }

    free(superblock_buffer);

    const size_t invalid_block_count = 1 + disk->superblock.fat_block_count;

    int32_t *fat = calloc(disk->superblock.block_count, sizeof(int32_t));
    if(!fat) {
        return -1;
    }

    // Setup intial FAT state
    memset(fat, FFS_BLOCK_INVALID, invalid_block_count);
    memset(fat + invalid_block_count, FFS_BLOCK_FREE, disk->superblock.block_count - invalid_block_count);

    // Write FAT block by block
    for(size_t i = 0; i < disk->superblock.fat_block_count; ++i) {
        if(ffs_block_write(disk, FFS_BLOCK_FAT + i, ((char *) fat) + i * disk->superblock.block_size) != 0) {
			free(fat);
			return -1;
		}
    }

	free(fat);

	// Setup root directory
	time_t current_time = time(NULL);
    struct ffs_directory root_directory = {
		.name = "/",
		.create_time = current_time,
		.modify_time = current_time,
		.access_time = current_time,
		.length = 0,
		.start_block = FFS_BLOCK_LAST,
		.unused = 0
    };

	ffs_address root_address = {disk->superblock.root_block, 0};
	if(ffs_dir_write(disk, root_address, &root_directory) != 0) {
		return -1;
	}

	return 0;
}

ffs_disk ffs_disk_open(const char *path, int mode)
{
    ffs_disk disk = malloc(sizeof(struct ffs_disk_info));
    if(!disk) {
        return NULL;
    }

	// Open disk file with correct mode
	switch(mode) {
		case FFS_DISK_OPEN_RDONLY:
			disk->file = fopen(path, "r+");
			break;
		case FFS_DISK_OPEN_RDWR:
			disk->file = fopen(path, "w+");
			break;
		default:
			free(disk);
			return NULL;
	}

    // Disk file could not be opened
    if(!disk->file) {
		free(disk);
        return NULL;
    }

    // Block size needs to be known to read superblock
    disk->superblock.block_size = FFS_DISK_BLOCK_SIZE;

    // Need a buffer to store a block
    struct ffs_superblock *superblock_buffer = malloc(FFS_DISK_BLOCK_SIZE);
    if(!superblock_buffer) {
		free(disk);
        return NULL;
    }

    // Cache disk superblock
    ffs_block_read(disk, FFS_BLOCK_SUPERBLOCK, superblock_buffer);
    disk->superblock = *superblock_buffer;

    free(superblock_buffer);

    return disk;
}

const struct ffs_superblock *ffs_disk_superblock(const ffs_disk disk)
{
    // Disk must not be NULL
    if(!disk) {
        return NULL;
    }

    return &disk->superblock;
}
