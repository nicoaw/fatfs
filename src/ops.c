#include "ops.h"
#include "aux.h"
#include "block.h"
#include "dir.h"
#include "disk.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

// Get currently mounted disk from fuse context
#define FATFS_DISK(context)	(context->private_data)

// A directory address and entry
struct directory
{
	address address;
	struct entry entry;
};

// Get directory address and entry from path
// Returns non-zero on failure
int get_directory(disk disk, const char *path, struct directory *directory);

// Remove directory at path
// Returns non-zero on failure
int remove_directory(disk disk, const char *path);

// Make new directory at path with flags
// Returns non-zero on failure
int make_directory(disk disk, const char *path, uint32_t flags);

// Split path into base path and directory name
// Path will be updated to base path
// Returns directory name offset in path
char *split_path(char *path);

int get_directory(disk disk, const char *path, struct directory *directory)
{
	syslog(LOG_DEBUG, "retreiving directory '%s'", path);

	const struct superblock *sb = disk_superblock(disk);

	// Find directory address
	directory->address = dir_find(disk, path);
	if(!DIR_ADDRESS_VALID(sb, directory->address)) {
		return -1;
	}

	// Read directory entry
	if(dir_read(disk, directory->address, DIR_ENTRY_OFFSET, &directory->entry, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	syslog(LOG_DEBUG, "retrevied directory '%s'", path);
	return 0;
}

int remove_directory(disk disk, const char *path)
{
	syslog(LOG_INFO, "removing directory '%s'", path);
	
	const struct superblock *sb = disk_superblock(disk);

	char *basepath = calloc(strlen(path), sizeof(char)); // Need mutable path to split
	strcpy(basepath, path);
	const char *name = split_path(basepath);

	// Need parent directory to free space from unlinked directory
	struct directory parent;
	if(get_directory(disk, basepath, &parent) != 0) {
		return -ENOENT;
	}

	// Read parent directory entries
	struct entry *entries = malloc(parent.entry.size);
	if(dir_read(disk, parent.address, 0, entries, parent.entry.size) != parent.entry.size) {
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
		free(entries);
		return -ENOENT;
	}

	// Free last entry space
	if(dir_free(disk, parent.address, sizeof(struct entry)) != 0) {
		free(entries);
		return -ENOENT;
	}
	
	free(entries);
	syslog(LOG_NOTICE, "removed directory '%s'", path);
	return 0;
}

int make_directory(disk disk, const char *path, uint32_t flags)
{
	syslog(LOG_INFO, "making directory '%s'", path);

	const struct superblock *sb = disk_superblock(disk);

	char *basepath = calloc(strlen(path), sizeof(char)); // Need mutable path to split
	strcpy(basepath, path);
	const char *name = split_path(basepath);

	// Make sure name is not too long
	if(strlen(name) > DIR_NAME_LENGTH) {
		syslog(LOG_ERR, "directory name too long '%s'", name);
		return -ENOENT;
	}

	// Need parent directory to allocate space for new directory
	struct directory parent;
	if(get_directory(disk, basepath, &parent) != 0) {
		return -ENOENT;
	}

	// Allocate space for new directory
	if(dir_alloc(disk, parent.address, sizeof(struct entry)) != 0) {
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
		return -ENOENT;
	}
	
	syslog(LOG_NOTICE, "created directory '%s'", path);
	return 0;
}

int fatfs_getattr(const char *path, struct stat *stats)
{
	syslog(LOG_INFO, "retreiving attributes for directory '%s'", path);

	struct fuse_context *context = fuse_get_context();
	disk disk = FATFS_DISK(fuse_get_context());

	const struct superblock *sb = disk_superblock(disk);

	// Need directory entry information
	struct directory directory;
	if(get_directory(disk, path, &directory) != 0) {
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

	syslog(LOG_NOTICE, "retreived attributes for directory '%s'", path);
	return 0;
}

int fatfs_mkdir(const char *path, mode_t mode)
{
	disk disk = FATFS_DISK(fuse_get_context());
	return make_directory(disk, path, DIR_DIRECTORY);
}

int fatfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	disk disk = FATFS_DISK(fuse_get_context());
	return make_directory(disk, path, DIR_FILE);
}

int fatfs_open(const char *path, struct fuse_file_info *file_info)
{
	syslog(LOG_INFO, "opening directory '%s'", path);

	disk disk = FATFS_DISK(fuse_get_context());

	// Need to check if directory exists
	struct directory directory;
	if(get_directory(disk, path, &directory) != 0) {
		return -ENOENT;
	}

	syslog(LOG_NOTICE, "opened directory '%s'", path);
	return 0;
}

int fatfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	syslog(LOG_INFO, "reading %zu bytes at offset %zd for directory '%s'", size, offset, path);

	disk disk = FATFS_DISK(fuse_get_context());

	const struct superblock *sb = disk_superblock(disk);

	address address = dir_find(disk, path);
	if(!DIR_ADDRESS_VALID(sb, address)) {
		return -ENOENT;
	}

	// Can't read from this offset since it will try to read the entry
	if((uint32_t) offset == DIR_ENTRY_OFFSET) {
		syslog(LOG_ERR, "offset %u out of range", offset);
		return -ENOENT;
	}

	int read = dir_read(disk, address, offset, buffer, size);
	memset(buffer + read, 0, size - read); // Fill rest of buffer with zeros
	syslog(LOG_NOTICE, "read %u bytes at offset %u for directory '%s'", read, offset, path);
	return read;
}

int fatfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info)
{
	syslog(LOG_INFO, "reading entries for directory '%s'", path);

	disk disk = FATFS_DISK(fuse_get_context());

	// Fill automatically created links
	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	// Get parent directory
	struct directory directory;
	if(get_directory(disk, path, &directory) != 0) {
		return -ENOENT;
	}

	// Read parent directory entries
	struct entry *entries = malloc(directory.entry.size);
	if(dir_read(disk, directory.address, 0, entries, directory.entry.size) != directory.entry.size) {
		free(entries);
		return -ENOENT;
	}

	// Fill directory entry information
	for(uint32_t i = 0; i < directory.entry.size / sizeof(struct entry); ++i) {
		filler(buffer, entries[i].name, NULL, 0);
	}

	free(entries);
	syslog(LOG_NOTICE, "read entries for directory '%s'", path);
	return 0;
}

int fatfs_rmdir(const char *path)
{
	disk disk = FATFS_DISK(fuse_get_context());
	return remove_directory(disk, path);
}

int fatfs_unlink(const char *path)
{
	disk disk = FATFS_DISK(fuse_get_context());
	return remove_directory(disk, path);
}

int fatfs_utimens(const char *path, const struct timespec tv[2])
{
	syslog(LOG_INFO, "changing access time %lu and modify time %lu for directory '%s'", tv[0].tv_sec, tv[1].tv_sec, path);

	disk disk = FATFS_DISK(fuse_get_context());

	const struct superblock *sb = disk_superblock(disk);

	// Need directory entry information
	struct directory directory;
	if(get_directory(disk, path, &directory) != 0) {
		return -ENOENT;
	}

	// Update access an modify times
	directory.entry.access_time = tv[0].tv_sec;
	directory.entry.modify_time = tv[1].tv_sec;

	// Write changes
	if(dir_write(disk, directory.address, DIR_ENTRY_OFFSET, &directory.entry, sizeof(struct entry)) != sizeof(struct entry)) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "changed access time %lu and modify time %lu for directory '%s'", directory.entry.access_time, directory.entry.modify_time, path);
	return 0;
}

char *split_path(char *path)
{
	char *dividor = strrchr(path, '/');

	*dividor = '\0';
	return dividor + 1;
}
