#include <ffs/ffs_block.h>
#include <ffs/ffs_debug.h>
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
int ffs_block_read(ffs_disk disk, uint32_t block, void *buffer)
{
	FFS_LOG(0, "disk=%p block=%d buffer=%p", disk, block, buffer);

	if(block == FFS_BLOCK_LAST || block == FFS_BLOCK_INVALID) {
		FFS_ERR(0, "specified block invalid");
		return -1;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
		FFS_ERR(0, "superblock retrieval failed");
        return -1;
    }

	// Seek to block position
	if(fseek(disk->file, block * superblock->block_size, SEEK_SET) != 0) {
		FFS_ERR(0, "block seek failed");
		return -1;
	}

	// Read entire block
	if(fread(buffer, superblock->block_size, 1, disk->file) != 1) {
		FFS_ERR(0, "block read failed");
		return -1;
	}

	return 0;
}

// Must be defined here because ffs_disk is defined here
int ffs_block_write(ffs_disk disk, uint32_t block, const void *buffer)
{
	FFS_LOG(0, "disk=%p block=%d buffer=%p", disk, block, buffer);

	if(block == FFS_BLOCK_LAST || block == FFS_BLOCK_INVALID) {
		FFS_ERR(0, "specified block invalid");
		return -1;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
		FFS_ERR(0, "superblock retrieval failed");
        return -1;
    }

	// Seek to block position
	if(fseek(disk->file, block * superblock->block_size, SEEK_SET) != 0) {
		FFS_ERR(0, "block seek failed");
		return -1;
	}

	// Read entire block
	if(fwrite(buffer, superblock->block_size, 1, disk->file) != 1) {
		FFS_ERR(0, "block write failed");
		return -1;
	}

	return 0;
}

int ffs_disk_close(ffs_disk disk)
{
	FFS_LOG(1, "disk=%p", disk);

    // Disk must not be NULL
    if(!disk) {
		FFS_ERR(1, "specified disk is invalid");
        return -1;
    }

    // Must be able to close disk file
    if(fclose(disk->file) != 0) {
		FFS_ERR(1, "disk close failed");
        return -1;
    }

    // Done using disk
    free(disk);

    return 0;
}

int ffs_disk_init(ffs_disk disk, uint32_t block_count)
{
	FFS_LOG(1, "disk=%p block_count=%zd", disk, block_count);

    const uint32_t fat_size = block_count * sizeof(uint32_t);

    // Setup disk superblock
    disk->superblock.magic = 0x2345beef;
    disk->superblock.block_count = block_count;
    disk->superblock.block_size = FFS_DISK_BLOCK_SIZE;
    disk->superblock.fat_block_count = fat_size / disk->superblock.block_size + (fat_size % disk->superblock.block_size > 0);
	disk->superblock.root_block = 1 + disk->superblock.fat_block_count;

	char *buffer = malloc(disk->superblock.block_size);
	for(uint32_t i = 0; i < disk->superblock.block_size; ++i) {
		buffer[i] = 0;
	}

	for(uint32_t i = 0; i < disk->superblock.block_count; ++i) {
		ffs_block_write(disk, i, buffer);
	}
	free(buffer);

    // Need a buffer to store a block
    struct ffs_superblock *superblock_buffer = malloc(disk->superblock.block_size);
    if(!superblock_buffer) {
		FFS_ERR(1, "superblock buffer allocation failed");
        return -1;
    }

    *superblock_buffer = disk->superblock;

    // Write superblock to be read when the disk is opened
    if(ffs_block_write(disk, FFS_BLOCK_SUPERBLOCK, superblock_buffer) != 0) {
		free(superblock_buffer);
		FFS_ERR(1, "superblock write failed");
        return -1;
    }

    free(superblock_buffer);

	uint32_t *fat = malloc(disk->superblock.block_count * sizeof(uint32_t));
    if(!fat) {
		FFS_ERR(1, "FAT buffer allocation failed");
        return -1;
    }

	for(uint32_t i = 0; i < disk->superblock.block_count; ++i) {
		if(i == disk->superblock.root_block) {
			fat[i] = FFS_BLOCK_LAST;
		} else if(i < disk->superblock.fat_block_count + 1) {
			fat[i] = FFS_BLOCK_INVALID;
		} else {
			fat[i] = FFS_BLOCK_FREE;
		}
	}

	// Write FAT block by block
	for(uint32_t i = 0; i < disk->superblock.fat_block_count; ++i) {
		if(ffs_block_write(disk, FFS_BLOCK_FAT + i, ((uint8_t *) fat) + i * disk->superblock.block_size) != 0) {
			FFS_ERR(1, "FAT block write failed");
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
		.flags = FFS_DIR_DIRECTORY,
		.unused = 0
    };

	ffs_address root_address = ffs_dir_root(disk);
	if(ffs_dir_write(disk, root_address, &root_directory) != 0) {
		FFS_ERR(1, "root directory write failed");
		return -1;
	}

	return 0;
}

ffs_disk ffs_disk_open(const char *path)
{
	FFS_LOG(1, "path=%s", path);

    ffs_disk disk = malloc(sizeof(struct ffs_disk_info));
    if(!disk) {
		FFS_ERR(1, "disk allocation failed");
        return NULL;
    }

	// Open existing or non-existing disk file
	disk->file = fopen(path, "r+");
	if(!disk->file) {
		disk->file = fopen(path, "w+");
	}

    // Disk file could not be opened
    if(!disk->file) {
		free(disk);
		FFS_ERR(1, "disk file is invalid");
        return NULL;
    }

    // Block size needs to be known to read superblock
    disk->superblock.block_size = FFS_DISK_BLOCK_SIZE;

    // Need a buffer to store a block
    struct ffs_superblock *superblock_buffer = malloc(FFS_DISK_BLOCK_SIZE);
    if(!superblock_buffer) {
		free(disk);
		FFS_ERR(1, "superblock buffer allocation failed");
        return NULL;
    }

    // Cache disk superblock
    ffs_block_read(disk, FFS_BLOCK_SUPERBLOCK, superblock_buffer);
    disk->superblock = *superblock_buffer;

    free(superblock_buffer);
    disk->superblock.block_size = FFS_DISK_BLOCK_SIZE;

    return disk;
}

const struct ffs_superblock *ffs_disk_superblock(const ffs_disk disk)
{
	FFS_LOG(1, "disk=%p", disk);

    // Disk must not be NULL
    if(!disk) {
		FFS_ERR(1, "specified disk invalid");
        return NULL;
    }

    return &disk->superblock;
}
