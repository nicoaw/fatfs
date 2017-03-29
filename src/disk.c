#include "block.h"
#include "disk.h"
#include "entry.h"
#include <fuse.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define DISK_BLOCK_SIZE	1024

struct disk_info {
    FILE *file;
    struct superblock superblock;
};

// Read or write block entire contents of block
// Offset must be valid
// Buffer must be size of a block
// Returns zero on success; otherwise, returns non-zero
int block_readwrite(disk disk, block offset, void *readbuf, const void *writebuf);

// Must be defined here because disk is defined here
int block_read(disk disk, block offset, void *buffer)
{
	return block_readwrite(disk, offset, buffer, NULL);
}

int block_readwrite(disk disk, block offset, void *readbuf, const void *writebuf)
{
	syslog(LOG_DEBUG, "%s%s%s block %u", readbuf ? "reading" : "", (readbuf && writebuf) ? "/" : "", writebuf ? "writing" : "", offset);

	// Offset must be valid
	if(!BLOCK_VALID(offset)) {
		syslog(LOG_ERR, "invalid block %u", offset);
		return -1;
	}

    const struct superblock *sb = disk_superblock(disk);

	// Seek to block position
	if(fseek(disk->file, offset * sb->block_size, SEEK_SET) != 0) {
		syslog(LOG_ERR, "failed to seek disk to block %u", offset);
		return -1;
	}

	if(readbuf) {
		// Read entire block
		if(fread(readbuf, sb->block_size, 1, disk->file) != 1) {
			syslog(LOG_ERR, "failed to read block %u", offset);
			return -1;
		}
	}

	if(writebuf) {
		// Write entire block
		if(fwrite(writebuf, sb->block_size, 1, disk->file) != 1) {
			syslog(LOG_ERR, "failed to write block %u", offset);
			return -1;
		}
	}

	syslog(LOG_DEBUG, "%s%s%s block %u", readbuf ? "read" : "", (readbuf && writebuf) ? "/" : "", writebuf ? "wrote" : "", offset);
	return 0;
}

// Must be defined here because disk is defined here
int block_write(disk disk, block offset, const void *buffer)
{
	return block_readwrite(disk, offset, NULL, buffer);
}

int disk_close(disk disk)
{
	syslog(LOG_DEBUG, "closing disk");

    // Must be able to close disk file
    if(fclose(disk->file) != 0) {
		syslog(LOG_ERR, "failed to close disk");
        return -1;
    }

    // Done using disk
    free(disk);
	syslog(LOG_INFO, "closed disk");
    return 0;
}

int disk_format(disk disk, struct superblock sb)
{
	syslog(LOG_DEBUG, "formating disk: magic %x, block count %u, fat_block_count %u, block size %u, root block %u",
			sb.magic,
			sb.block_count,
			sb.fat_block_count,
			sb.block_size,
			sb.root_block);

	disk->superblock = sb;

	void *buffer = malloc(disk->superblock.block_size);
	memset(buffer, 0, disk->superblock.block_size);

	// Fill disk with zeros
	for(block i = 0; i < disk->superblock.block_count; ++i) {
		block_write(disk, i, buffer);
	}

	// Write superblock on disk
	memcpy(buffer, &disk->superblock, sizeof(struct superblock));
	if(block_write(disk, BLOCK_SUPERBLOCK, buffer) != 0) {
		return -1;
	}

	free(buffer);

	block *fat_buffer = malloc(disk->superblock.block_count * sizeof(block));
	for(block i = 0; i < disk->superblock.block_count; ++i) {
		if(i == disk->superblock.root_block) {
			fat_buffer[i] = BLOCK_LAST;
		} else if(i < disk->superblock.fat_block_count + 1) {
			fat_buffer[i] = BLOCK_INVALID;
		} else {
			fat_buffer[i] = BLOCK_FREE;
		}
	}

	// Write FAT block by block
	for(uint32_t i = 0; i < disk->superblock.fat_block_count; ++i) {
		if(block_write(disk, BLOCK_FAT + i, ((uint8_t *) fat_buffer) + i * disk->superblock.block_size) != 0) {
			free(fat_buffer);
			return -1;
		}
	}
	
	free(fat_buffer);

	// Setup root directory
	time_t current_time = time(NULL);
    struct entry ent = {
		.name = "/",
		.create_time = current_time,
		.modify_time = current_time,
		.access_time = current_time,
		.size = 0,
		.start_block = BLOCK_LAST,
		.mode = S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IXOTH,
		.unused = 0
    };

	address root = {sb.root_block, sizeof(struct entry)};
	if(dir_write(disk, root, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	syslog(LOG_INFO, "formatted disk: magic %x, block count %u, fat_block_count %u, block size %u, root block %u",
			sb.magic,
			sb.block_count,
			sb.fat_block_count,
			sb.block_size,
			sb.root_block);
	return 0;
}

disk disk_open(const char *path)
{
	syslog(LOG_DEBUG, "opening disk '%s'", path);

    disk disk = malloc(sizeof(struct disk_info));

	// Open existing or non-existing disk file
	disk->file = fopen(path, "r+");
	if(!disk->file) {
		disk->file = fopen(path, "w+");
	}

    // Disk file could not be opened
    if(!disk->file) {
		free(disk);
		syslog(LOG_ERR, "failed to open disk %s", path);
        return NULL;
    }

	// Read existing superblock on disk
	// Can't use block_read since block size size is unknown
	fread(&disk->superblock, sizeof(struct superblock), 1, disk->file);

	syslog(LOG_INFO, "opened disk '%s'", path);
    return disk;
}

const struct superblock *disk_superblock(const disk disk)
{
    return &disk->superblock;
}
