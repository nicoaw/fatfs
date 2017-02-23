#include <ffs/ffs_aux.h>
#include <ffs/ffs_block.h>
#include <stdlib.h>

ffs_block ffs_block_alloc(ffs_disk disk, ffs_block next)
{
	FFS_LOG(0, "disk=%p next=%u", disk, next);

	// Next must be valid or FFS_BLOCK_LAST
	if(next != FFS_BLOCK_LAST && !FFS_BLOCK_VALID(next)) {
		FFS_ERR(0, "next block invalid");
		return FFS_BLOCK_INVALID;
	}

    const struct ffs_superblock *sb = ffs_disk_superblock(disk);
	ffs_block *fat_buffer = malloc(sb->block_size);

	// Find a free block using FAT
	for(uint32_t i = 0; i < sb->fat_block_count; ++i) {
		// Read FAT
		const ffs_block fat = FFS_BLOCK_FAT + i;
		if(ffs_block_read(disk, fat, fat_buffer) != 0) {
			free(fat_buffer);
			FFS_ERR(0, "FAT read failed");
			return FFS_BLOCK_INVALID;
		}

		for(uint32_t j = 0; j < FFS_BLOCK_FAT_ENTRY_COUNT(sb); ++j) {
			// Found free block
			if(fat_buffer[j] == FFS_BLOCK_FREE) {
				const ffs_block allocated = j + i * FFS_BLOCK_FAT_ENTRY_COUNT(sb);
				fat_buffer[j] = next;

				// Write updated FAT
				if(ffs_block_write(disk, fat, fat_buffer) != 0) {
					free(fat_buffer);
					FFS_ERR(0, "FAT write failed");
					return FFS_BLOCK_INVALID;
				}

				free(fat_buffer);
				return allocated;
			}
		}
	}

	free(fat_buffer);
	FFS_ERR(0, "failed to find free block");
	return FFS_BLOCK_INVALID;
}

int ffs_block_free(ffs_disk disk, ffs_block head)
{
	FFS_LOG(0, "disk=%p head=%u", disk, head);

	// Head must be valid
	if(!FFS_BLOCK_VALID(head)) {
		FFS_ERR(0, "head block invalid");
		return -1;
	}

    const struct ffs_superblock *sb = ffs_disk_superblock(disk);
	const ffs_block fat = FFS_BLOCK_FAT_BLOCK(sb, head);
	ffs_block *fat_buffer = malloc(sb->block_size);

	// Read FAT
	if(ffs_block_read(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		FFS_ERR(0, "FAT read failed");
		return -1;
	}

	fat_buffer[FFS_BLOCK_FAT_ENTRY(sb, head)] = FFS_BLOCK_FREE;

	// Write updated FAT
	if(ffs_block_write(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		FFS_ERR(0, "FAT read failed");
		return -1;
	}

	free(fat_buffer);
	return 0;
}

ffs_block ffs_block_next(ffs_disk disk, ffs_block previous)
{
	FFS_LOG(0, "disk=%p previous=%u", disk, previous);

	// Previous must be valid
	if(!FFS_BLOCK_VALID(previous)) {
		FFS_ERR(0, "previous block invalid");
		return FFS_BLOCK_INVALID;
	}

    const struct ffs_superblock *sb = ffs_disk_superblock(disk);
	const ffs_block fat = FFS_BLOCK_FAT_BLOCK(sb, previous);
	ffs_block *fat_buffer = malloc(sb->block_size);

	// Read FAT
	if(ffs_block_read(disk, fat, fat_buffer) != 0) {
		free(fat_buffer);
		FFS_ERR(0, "FAT read failed");
		return FFS_BLOCK_INVALID;
	}

	// Get next block according to FAT
	ffs_block next = fat_buffer[FFS_BLOCK_FAT_ENTRY(sb, previous)];
	free(fat_buffer);
	return next;
}
