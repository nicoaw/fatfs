#include <ffs/ffs_block.h>
#include <ffs/ffs_debug.h>
#include <stdlib.h>

#define FFS_BLOCK_FAT_ENTRY_COUNT(block_size)	(block_size / sizeof(uint32_t))
#define FFS_BLOCK_FAT_BLOCK(block_size, block)	(FFS_BLOCK_FAT + block / FFS_BLOCK_FAT_ENTRY_COUNT(block_size))
#define FFS_BLOCK_FAT_ENTRY(block_size, block)	(block % FFS_BLOCK_FAT_ENTRY_COUNT(block_size))

int ffs_block_alloc(ffs_disk disk, uint32_t parent_block)
{
	FFS_LOG("disk=%p parent_block=%d", disk, parent_block);

	if(parent_block == FFS_BLOCK_INVALID) {
		FFS_ERR("specified parent block invalid");
		return FFS_BLOCK_INVALID;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
		FFS_ERR("superblock retrieval failed");
        return FFS_BLOCK_INVALID;
    }

	const uint32_t fat_block_entry_count = FFS_BLOCK_FAT_ENTRY_COUNT(superblock->block_size);

	// Find a free block using FAT
	uint32_t *fat = malloc(superblock->block_size);
	for(uint32_t i = 0; i < superblock->fat_block_count; ++i) {
		const uint32_t fat_block = FFS_BLOCK_FAT + i;

		if(ffs_block_read(disk, fat_block, fat) != 0) {
			free(fat);
			FFS_ERR("FAT block read failed");
			return FFS_BLOCK_INVALID;
		}

		for(uint32_t j = 0; j < fat_block_entry_count; ++j) {
			// Found free block
			if(fat[j] == FFS_BLOCK_FREE) {
				const uint32_t block = j + i * fat_block_entry_count;
				fat[j] = FFS_BLOCK_LAST;

				if(ffs_block_write(disk, fat_block, fat) != 0) {
					free(fat);
					FFS_ERR("FAT block write failed");
					return FFS_BLOCK_INVALID;
				}

				// Set parent block's next
				if(parent_block != FFS_BLOCK_LAST) {
					const uint32_t parent_fat_block = FFS_BLOCK_FAT_BLOCK(superblock->block_size, parent_block);
					const uint32_t parent_fat_entry = FFS_BLOCK_FAT_ENTRY(superblock->block_size, parent_block);

					if(ffs_block_read(disk, parent_fat_block, fat) != 0) {
						free(fat);
						FFS_ERR("parent FAT block read failed");
						return FFS_BLOCK_INVALID;
					}

					fat[FFS_BLOCK_FAT_ENTRY(superblock->block_size, parent_block)] = block;

					if(ffs_block_write(disk, parent_fat_block, fat) != 0) {
						free(fat);
						FFS_ERR("parent FAT block write failed");
						return FFS_BLOCK_INVALID;
					}
				}

				free(fat);
				return block;
			}
		}
	}

	free(fat);
	FFS_ERR("block allocation failed");
	return FFS_BLOCK_INVALID;
}

int ffs_block_free(ffs_disk disk, uint32_t parent_block, uint32_t block)
{
	FFS_LOG("disk=%p parent_block=%d block=%d", disk, parent_block, block);

	if(parent_block == FFS_BLOCK_LAST || block == FFS_BLOCK_INVALID || block == FFS_BLOCK_LAST) {
		FFS_ERR("specified parent block or block invalid");
		return -1;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
		FFS_ERR("superblock retrieval failed");
        return -1;
    }

	uint32_t *fat = malloc(superblock->block_size);

	// Unlink child block from parent
	if(parent_block != FFS_BLOCK_INVALID) {
		const uint32_t parent_fat_block = FFS_BLOCK_FAT_BLOCK(superblock->block_size, parent_block);
		if(ffs_block_read(disk, parent_fat_block, fat) != 0) {
			free(fat);
			FFS_ERR("parent FAT block read failed");
			return -1;
		}

		fat[FFS_BLOCK_FAT_ENTRY(superblock->block_size, parent_block)] = FFS_BLOCK_LAST;

		if(ffs_block_write(disk, parent_fat_block, fat) != 0) {
			free(fat);
			FFS_ERR("parent FAT block write failed");
			return -1;
		}
	}

	// Free child blocks in block list
	// Doesn't use block_next for effeciency, but might be wise since DRY
	uint32_t fat_block = FFS_BLOCK_FAT_BLOCK(superblock->block_size, block);
	uint32_t fat_entry = FFS_BLOCK_FAT_ENTRY(superblock->block_size, block);
	uint32_t next_block;
	while(1) {
		if(ffs_block_read(disk, fat_block, fat) != 0) {
			free(fat);
			FFS_ERR("FAT block read failed");
			return -1;
		}

		next_block = fat[fat_entry];
		fat[fat_entry] = FFS_BLOCK_FREE;

		if(ffs_block_write(disk, fat_block, fat) != 0) {
			free(fat);
			FFS_ERR("FAT block write failed");
			return -1;
		}

		if(next_block == FFS_BLOCK_LAST) {
			free(fat);
			return 0;
		}

		block = next_block;
		fat_block = FFS_BLOCK_FAT_BLOCK(superblock->block_size, block);
		fat_entry = FFS_BLOCK_FAT_ENTRY(superblock->block_size, block);
	}

	free(fat);
	FFS_ERR("block free failed");
	return -1;
}

int ffs_block_next(ffs_disk disk, uint32_t block)
{
	FFS_LOG("disk=%p block=%d", disk, block);

	if(block == FFS_BLOCK_LAST || block == FFS_BLOCK_INVALID) {
		FFS_ERR("specified block invalid");
		return FFS_BLOCK_LAST;
	}

    const struct ffs_superblock *superblock = ffs_disk_superblock(disk);
    if(!superblock) {
		FFS_ERR("superblock retrieval failed");
        return FFS_BLOCK_LAST;
    }

	const uint32_t fat_block = FFS_BLOCK_FAT_BLOCK(superblock->block_size, block);

	uint32_t *fat = malloc(superblock->block_size);
	if(ffs_block_read(disk, fat_block, fat) != 0) {
		free(fat);
		FFS_ERR("FAT block read failed");
		return FFS_BLOCK_LAST;
	}

	// Get next block according to FAT
	uint32_t next_block = fat[FFS_BLOCK_FAT_ENTRY(superblock->block_size, block)];
	free(fat);
	return next_block;
}
