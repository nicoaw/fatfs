#include "ops.h"
#include "aux.h"
#include "block.h"
#include "dir.h"
#include "disk.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Get currently mounted disk from fuse context
#define FATFS_DISK(context)	(context->private_data)

// A directory address and entry
struct fatfs_directory
{
	address address;
	struct entry entry;
};

// Get directory address and entry from path
// Returns non-zero on failure
int fatfs_get_directory(disk disk, const char *path, struct fatfs_directory *directory);

// Remove directory at path
// Returns non-zero on failure
int fatfs_remove_directory(disk disk, const char *path);

// Make new directory at path with flags
// Returns non-zero on failure
int fatfs_make_directory(disk disk, const char *path, uint32_t flags);

// Split path into base path and directory name
// Path will be updated to base path
// Returns directory name offset in path
char *split_path(char *path);

int fatfs_get_directory(disk disk, const char *path, struct fatfs_directory *directory)
{
	const struct superblock *sb = disk_superblock(disk);

	// Find directory address
	directory->address = dir_find(disk, path);
	if(!DIR_ADDRESS_VALID(sb, directory->address)) {
		ERR(2, "failed to find address");
		return -1;
	}

	// Read directory entry
	if(dir_read(disk, directory->address, DIR_ENTRY_OFFSET, &directory->entry, sizeof(struct entry)) != sizeof(struct entry)) {
		ERR(2, "failed to read entry");
		return -1;
	}

	return 0;
}

int fatfs_remove_directory(disk disk, const char *path)
{
	const struct superblock *sb = disk_superblock(disk);

	char *basepath = calloc(strlen(path), sizeof(char)); // Need mutable path to split
	strcpy(basepath, path);
	const char *name = split_path(basepath);

	// Need parent directory to free space from unlinked directory
	struct fatfs_directory parent;
	if(fatfs_get_directory(disk, basepath, &parent) != 0) {
		ERR(2, "failed to get parent directory");
		return -ENOENT;
	}

	// Read parent directory entries
	struct entry *entries = malloc(parent.entry.size);
	if(dir_read(disk, parent.address, 0, entries, parent.entry.size) != parent.entry.size) {
		ERR(2, "failed to read parent directory");
		free(entries);
		return -ENOENT;
	}

	// Count of directories in parent
	const uint32_t entry_count = parent.entry.size / sizeof(struct entry);

	for(uint32_t i = 0; i < entry_count; ++i) {
		// Found directory to delete
		if(strcmp(entries[i].name, name) == 0) {
			// Move last entry to deleted entry
			memcpy(entries + i, entries + (entry_count - 1), sizeof(struct entry));
			break;
		}
	}
	
	// Write modified parent directory entries
	if(dir_write(disk, parent.address, 0, entries, parent.entry.size) != parent.entry.size) {
		ERR(2, "failed to read parent directory");
		free(entries);
		return -ENOENT;
	}

	// Free last entry space
	if(dir_free(disk, parent.address, sizeof(struct entry)) != 0) {
		ERR(2, "failed to free last entry space");
		free(entries);
		return -ENOENT;
	}
	
	free(entries);
	return 0;
}

int fatfs_make_directory(disk disk, const char *path, uint32_t flags)
{
	const struct superblock *sb = disk_superblock(disk);

	char *basepath = calloc(strlen(path), sizeof(char)); // Need mutable path to split
	strcpy(basepath, path);
	const char *name = split_path(basepath);

	// Make sure name is not too long
	if(strlen(name) > DIR_NAME_LENGTH) {
		ERR(2, "name too long");
		return -ENOENT;
	}

	// Need parent directory to allocate space for new directory
	struct fatfs_directory parent;
	if(fatfs_get_directory(disk, basepath, &parent) != 0) {
		ERR(2, "failed to get parent directory");
		return -ENOENT;
	}

	// Allocate space for new directory
	if(dir_alloc(disk, parent.address, sizeof(struct entry)) != 0) {
		ERR(2, "failed to allocate new directory");
		return -ENOENT;
	}

	// Fill directory entry information
	time_t t = time(NULL);
	struct entry entry =
	{
    	.create_time = t,
    	.modify_time = t,
    	.access_time = t,
    	.size = 0,
    	.start_block = BLOCK_LAST,
    	.flags = flags,
    	.unused = 0,
	};
	strcpy(entry.name, name);

	// Write new directory
	if(dir_write(disk, parent.address, parent.entry.size, &entry, sizeof(entry)) != sizeof(entry)) {
		ERR(2, "failed to write new directory");
		return -ENOENT;
	}
	
	return 0;
}

int fatfs_getattr(const char *path, struct stat *stats)
{
	LOG(2, "path=%s stats=%p", path, stats);

	struct fuse_context *context = fuse_get_context();
	disk disk = FATFS_DISK(fuse_get_context());

	const struct superblock *sb = disk_superblock(disk);

	// Need directory entry information
	struct fatfs_directory directory;
	if(fatfs_get_directory(disk, path, &directory) != 0) {
		ERR(2, "failed to get directory");
		return -ENOENT;
	}

	// Initially clear stats
	memset(stats, 0, sizeof(*stats));

	stats->st_mode = S_IFDIR | 0777;
	stats->st_uid = context->uid;
	stats->st_gid = context->gid;
	stats->st_blksize = sb->block_size;
	stats->st_atime = directory.entry.access_time;
	stats->st_mtime = directory.entry.modify_time;
	stats->st_ctime = directory.entry.modify_time;

	// Count blocks allocated to file
	block block = directory.entry.start_block;
	while(block != BLOCK_LAST) {
		++stats->st_blocks;
		block = block_next(disk, block);
		if(block == BLOCK_INVALID) {
			ERR(2, "failed to get next block");
			return -ENOENT;
		}
	}

	if(directory.entry.flags & DIR_DIRECTORY) {
		// Directory is a directory
		stats->st_mode = S_IFDIR | 0555;
		stats->st_nlink = 2 + directory.entry.size / sizeof(struct entry); // Count all the . and .. links
	} else if(directory.entry.flags & DIR_FILE) {
		// Directory is a file
		stats->st_mode = S_IFREG | 0666;
		stats->st_nlink = 1;
		stats->st_size = directory.entry.size;
	}

	return 0;
}

int fatfs_mkdir(const char *path, mode_t mode)
{
	LOG(2, "path=%s mode=%u", path, mode);

	disk disk = FATFS_DISK(fuse_get_context());
	return fatfs_make_directory(disk, path, DIR_DIRECTORY);
}

int fatfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	LOG(2, "path=%s mode=%u dev=%u", path, mode, dev);

	disk disk = FATFS_DISK(fuse_get_context());
	return fatfs_make_directory(disk, path, DIR_FILE);
}

int fatfs_open(const char *path, struct fuse_file_info *file_info)
{
	LOG(2, "path=%s file_info=%p", path, file_info);

	disk disk = FATFS_DISK(fuse_get_context());

	// Need to check if directory exists
	struct fatfs_directory directory;
	if(fatfs_get_directory(disk, path, &directory) != 0) {
		return -ENOENT;
	}

	return 0;
}

int fatfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	LOG(2, "path=%s, buffer=%p, size=%zu, offset=%zd, file_info=%p", path, buffer, size, offset, file_info);

	disk disk = FATFS_DISK(fuse_get_context());

	const struct superblock *sb = disk_superblock(disk);

	address address = dir_find(disk, path);
	if(!DIR_ADDRESS_VALID(sb, address)) {
		ERR(2, "failed to find address");
		return -ENOENT;
	}

	// Can't read from this offset since it will try to read the entry
	if((uint32_t) offset == DIR_ENTRY_OFFSET) {
		ERR(2, "offset out of range");
		return -ENOENT;
	}

	int read = dir_read(disk, address, offset, buffer, size);
	memset(buffer + read, 0, size - read); // Fill rest of buffer with zeros
	return read;
}

int fatfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info)
{
	LOG(2, "path=%s, buffer=%p, filler=%p, offset=%zd, file_info=%p", path, buffer, filler, offset, file_info);

	disk disk = FATFS_DISK(fuse_get_context());

	// Fill automatically created links
	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	// Get parent directory
	struct fatfs_directory directory;
	if(fatfs_get_directory(disk, path, &directory) != 0) {
		ERR(2, "failed to get parent directory");
		return -ENOENT;
	}

	// Read parent directory entries
	struct entry *entries = malloc(directory.entry.size);
	if(dir_read(disk, directory.address, 0, entries, directory.entry.size) != directory.entry.size) {
		ERR(2, "failed to read parent directory entries");
		free(entries);
		return -ENOENT;
	}

	// Fill directory entry information
	for(uint32_t i = 0; i < directory.entry.size / sizeof(struct entry); ++i) {
		filler(buffer, entries[i].name, NULL, 0);
	}

	free(entries);
	return 0;
}

int fatfs_rmdir(const char *path)
{
	LOG(2, "path=%s", path);

	disk disk = FATFS_DISK(fuse_get_context());
	return fatfs_remove_directory(disk, path);
}

int fatfs_unlink(const char *path)
{
	LOG(2, "path=%s", path);

	disk disk = FATFS_DISK(fuse_get_context());
	return fatfs_remove_directory(disk, path);
}

int fatfs_utimens(const char *path, const struct timespec tv[2])
{
	LOG(2, "path=%s tv=%p", path, tv);

	disk disk = FATFS_DISK(fuse_get_context());

	const struct superblock *sb = disk_superblock(disk);

	// Need directory entry information
	struct fatfs_directory directory;
	if(fatfs_get_directory(disk, path, &directory) != 0) {
		ERR(2, "failed to get directory");
		return -ENOENT;
	}

	// Update access an modify times
	directory.entry.access_time = tv[0].tv_sec;
	directory.entry.modify_time = tv[1].tv_sec;

	// Write changes
	if(dir_write(disk, directory.address, DIR_ENTRY_OFFSET, &directory.entry, sizeof(struct entry)) != sizeof(struct entry)) {
		ERR(2, "failed to write changes");
		return -ENOENT;
	}

	return 0;
}

char *split_path(char *path)
{
	char *dividor = strrchr(path, '/');

	*dividor = '\0';
	return dividor + 1;
}