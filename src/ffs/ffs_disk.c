#include <ffs/ffs_aux.h>
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

// Read or write block entire contents of block
// Offset must be valid
// Buffer must be size of a block
// Returns zero on success; otherwise, returns non-zero
int ffs_block_readwrite(ffs_disk disk, ffs_block offset, void *readbuf, const void *writebuf);

// Must be defined here because ffs_disk is defined here
int ffs_block_read(ffs_disk disk, ffs_block offset, void *buffer)
{
	FFS_LOG(0, "disk=%p offset=%u buffer=%p", disk, offset, buffer);
	return ffs_block_readwrite(disk, offset, buffer, NULL);
}

int ffs_block_readwrite(ffs_disk disk, ffs_block offset, void *readbuf, const void *writebuf)
{
	FFS_LOG(0, "disk=%p offset=%u readbuf=%p writebuf=%p", disk, offset, readbuf, writebuf);

	// Offset must be valid
	if(!FFS_BLOCK_VALID(offset)) {
		FFS_ERR(0, "offset block invalid");
		return -1;
	}

    const struct ffs_superblock *sb = ffs_disk_superblock(disk);

	// Seek to block position
	if(fseek(disk->file, offset * sb->block_size, SEEK_SET) != 0) {
		FFS_ERR(0, "disk seek failed");
		return -1;
	}

	if(readbuf) {
		// Read entire block
		if(fread(readbuf, sb->block_size, 1, disk->file) != 1) {
			FFS_ERR(0, "failed to read buffer");
			return -1;
		}
	}

	if(writebuf) {
		// Write entire block
		if(fwrite(writebuf, sb->block_size, 1, disk->file) != 1) {
			FFS_ERR(0, "failed to write buffer");
			return -1;
		}
	}

	return 0;
}

// Must be defined here because ffs_disk is defined here
int ffs_block_write(ffs_disk disk, ffs_block offset, const void *buffer)
{
	FFS_LOG(0, "disk=%p offset=%u buffer=%p", disk, offset, buffer);
	return ffs_block_readwrite(disk, offset, NULL, buffer);
}


int ffs_disk_close(ffs_disk disk)
{
	FFS_LOG(1, "disk=%p", disk);

    // Must be able to close disk file
    if(fclose(disk->file) != 0) {
		FFS_ERR(1, "disk close failed");
        return -1;
    }

    // Done using disk
    free(disk);

    return 0;
}

int ffs_disk_format(ffs_disk disk, struct ffs_superblock sb)
{
	FFS_LOG(1, "disk=%p sb={magic=%u block_count=%u fat_block_count=%u block_size=%u root_block=%u}", disk, sb.magic, sb.block_count, sb.block_size, sb.root_block);

	/*
	   const uint32_t fat_size = block_count * sizeof(uint32_t);
	   disk->superblock.magic = 0x2345beef;
	   disk->superblock.block_count = block_count;
	   disk->superblock.block_size = FFS_DISK_BLOCK_SIZE;
	   disk->superblock.fat_block_count = fat_size / disk->superblock.block_size + (fat_size % disk->superblock.block_size > 0);
	   disk->superblock.root_block = 1 + disk->superblock.fat_block_count;
	   */

	disk->superblock = sb;

	void *buffer = malloc(disk->superblock.block_size);
	memset(buffer, 0, disk->superblock.block_size);

	// Fill disk with zeros
	for(ffs_block i = 0; i < disk->superblock.block_count; ++i) {
		ffs_block_write(disk, i, buffer);
	}

	// Write superblock on disk
	memcpy(buffer, &disk->superblock, sizeof(struct ffs_superblock));
	if(ffs_block_write(disk, FFS_BLOCK_SUPERBLOCK, buffer) != 0) {
		FFS_ERR(1, "superblock write failed");
		return -1;
	}

	free(buffer);

	ffs_block *fat_buffer = malloc(disk->superblock.block_count * sizeof(ffs_block));
    if(!fat_buffer) {
		FFS_ERR(1, "FAT buffer allocation failed");
        return -1;
    }

	for(ffs_block i = 0; i < disk->superblock.block_count; ++i) {
		if(i == disk->superblock.root_block) {
			fat_buffer[i] = FFS_BLOCK_LAST;
		} else if(i < disk->superblock.fat_block_count + 1) {
			fat_buffer[i] = FFS_BLOCK_INVALID;
		} else {
			fat_buffer[i] = FFS_BLOCK_FREE;
		}
	}

	// Write FAT block by block
	for(uint32_t i = 0; i < disk->superblock.fat_block_count; ++i) {
		if(ffs_block_write(disk, FFS_BLOCK_FAT + i, ((uint8_t *) fat_buffer) + i * disk->superblock.block_size) != 0) {
			FFS_ERR(1, "FAT write failed");
			free(fat_buffer);
			return -1;
		}
	}
	
	free(fat_buffer);

	// Setup root directory
	time_t current_time = time(NULL);
    struct ffs_entry directory = {
		.name = "/",
		.create_time = current_time,
		.modify_time = current_time,
		.access_time = current_time,
		.size = 0,
		.start_block = FFS_BLOCK_LAST,
		.flags = FFS_DIR_DIRECTORY,
		.unused = 0
    };

	ffs_address root = ffs_dir_find(disk, "/");
	if(ffs_dir_write(disk, root, FFS_DIR_ENTRY_OFFSET, &directory, sizeof(struct ffs_entry)) != sizeof(struct ffs_entry)) {
		FFS_ERR(1, "failed to write root directory");
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
		FFS_ERR(1, "file failed to open");
        return NULL;
    }

	// Read existing superblock on disk
	// Can't use block_read since block size size is unknown
	fread(&disk->superblock, sizeof(struct ffs_superblock), 1, disk->file);

    return disk;
}

const struct ffs_superblock *ffs_disk_superblock(const ffs_disk disk)
{
    return &disk->superblock;
}
