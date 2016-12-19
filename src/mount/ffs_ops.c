#include "ffs_ops.h"

#include <errno.h>
#include <ffs/ffs_block.h>
#include <ffs/ffs_debug.h>
#include <ffs/ffs_dir.h>
#include <ffs/ffs_disk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MOUNT_DISK ((ffs_disk) fuse_get_context()->private_data)

struct path_info
{
	ffs_address root_address;
	ffs_address address;
	struct ffs_directory directory;
};

// Get first (inclusive) and last (exculsive) block from size and offset starting at start block
// Returns zero on success
int get_block_range(uint32_t start_block, size_t size, off_t offset, uint32_t *first_block, uint32_t *last_block);

// Get path info given specified path
// Returns zero on success
int get_path_info(const char *path, struct path_info *path_info);

// Make a file or directory on specified path
// Returns zero on success
int make_directory(const char *path, uint32_t flags);

// Resize an existing directory
// If the directory previously was larger then length, the extra data is lost
// If the directory previously was shorter, it is extended with zeros
// Returns zero on success
int resize_directory(struct path_info *path_info, uint32_t length);

// Subtract a from b
// If result is negative then it returns zero
uint32_t safe_sub(uint32_t a, uint32_t b);

// Split path into base path and directory name
// Path will be updated to base path
// Returns directory name offset in original path
char *split_path(char *path);

int get_block_range(uint32_t start_block, size_t size, off_t offset, uint32_t *first_block, uint32_t *last_block)
{
	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		return -1;
	}

	// Find first block
	while(1) {
		if(start_block == FFS_BLOCK_INVALID || start_block == FFS_BLOCK_LAST) {
			return -1;
		}

		if(offset < superblock->block_size) {
			break;
		}

		start_block = ffs_block_next(MOUNT_DISK, start_block);
		offset -= superblock->block_size;
	}

	*first_block = start_block;
	offset += size; // New offset for last block

	while(1) {
		if(start_block == FFS_BLOCK_INVALID || start_block == FFS_BLOCK_LAST) {
			return -1;
		}

		if(offset < superblock->block_size) {
			break;
		}

		start_block = ffs_block_next(MOUNT_DISK, start_block);
		offset -= superblock->block_size;
	}

	*last_block = ffs_block_next(MOUNT_DISK, start_block);
	return 0;
}

int get_path_info(const char *path, struct path_info *path_info)
{
	// Get root address
	path_info->root_address = ffs_dir_root(MOUNT_DISK);
	if(ffs_dir_address_valid(MOUNT_DISK, path_info->root_address) != 0) {
		return -1;
	}

	// Get directory at specified path
	path_info->address = ffs_dir_path(MOUNT_DISK, path_info->root_address, path);
	if(ffs_dir_address_valid(MOUNT_DISK, path_info->address) != 0) {
		return -1;
	}

	if(ffs_dir_read(MOUNT_DISK, path_info->address, &path_info->directory) != 0) {
		return -1;
	}

	return 0;
}

int ffs_create(const char *path, mode_t mode, struct fuse_file_info *file_info)
{
	FFS_LOG(2, "path=%s, mode=%d, file_info=%p", path, mode, file_info);

	if(make_directory(path, FFS_DIR_FILE) != 0) {
		return -ENOENT;
	}

	return 0;
}

int ffs_getattr(const char *path, struct stat *stats)
{
	FFS_LOG(2, "path=%s, stats=%p", path, stats);

	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		return -ENOENT;
	}

	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		return -ENOENT;
	}

	memset(stats, 0, sizeof(*stats));
	stats->st_mode = S_IFDIR | 0777;
	stats->st_uid = getuid();
	stats->st_gid = getgid();
	stats->st_blksize = superblock->block_size;
	stats->st_blocks = superblock->block_count;
	stats->st_atime = pi.directory.access_time;
	stats->st_mtime = pi.directory.modify_time;
	stats->st_ctime = pi.directory.modify_time;

	if(pi.directory.flags == FFS_DIR_DIRECTORY) {
		stats->st_mode = S_IFDIR | 0555;
		stats->st_nlink = 2 + pi.directory.length / sizeof(struct ffs_directory);
	} else if(pi.directory.flags == FFS_DIR_FILE) {
		stats->st_mode = S_IFREG | 0666;
		stats->st_nlink = 1;
		stats->st_size = pi.directory.length;
	}

	return 0;
}

int ffs_open(const char *path, struct fuse_file_info *file_info)
{
	FFS_LOG(2, "path=%s, file_info=%p", path, file_info);

	// Need to check if file exists
	if(!(file_info->flags & O_CREAT)) {
		struct path_info pi;
		if(get_path_info(path, &pi) != 0) {
			return -ENOENT;
		}
	}

	return 0;
}

int ffs_mkdir(const char *path, mode_t mode)
{
	FFS_LOG(2, "path=%s, mode=%d", path, mode);

	if(make_directory(path, FFS_DIR_DIRECTORY) != 0) {
		return -ENOENT;
	}

	return 0;
}

int ffs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	FFS_LOG(2, "path=%s, buffer=%p, size=%zu, offset=%zd, file_info=%p", path, buffer, size, offset, file_info);

	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		return -ENOENT;
	}

	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		return -ENOENT;
	}

	uint32_t first_block;
	uint32_t last_block;
	if(get_block_range(pi.directory.start_block, size, offset, &first_block, &last_block) != 0) {
		return -ENOENT;
	}

	// Read block by block
	while(first_block != last_block) {
		if(ffs_block_read(MOUNT_DISK, first_block, buffer) != 0) {
			return -ENOENT;
		}

		first_block = ffs_block_next(MOUNT_DISK, first_block);
		buffer += superblock->block_size;
	}

	return 0;
}

int ffs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info)
{
	FFS_LOG(2, "path=%s, buffer=%p, filler=%p, offset=%zd, file_info=%p", path, buffer, filler, offset, file_info);

	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		return -ENOENT;
	}

	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		return -ENOENT;
	}

	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	// Fill directory entries
	ffs_address address = {pi.directory.start_block, 0};
	for(
			uint32_t length_scanned = 0;
			length_scanned < pi.directory.length;
			length_scanned += sizeof(struct ffs_directory), address = ffs_dir_next(MOUNT_DISK, address)) {
		struct ffs_directory directory;
		if(ffs_dir_read(MOUNT_DISK, address, &directory) != 0) {
			return -ENOENT;
		}

		filler(buffer, directory.name, NULL, 0);
	}

	return 0;
}

int ffs_unlink(const char *path)
{
	FFS_LOG(2, "path=%s", path);

	// Need a mutable path to split it
	char *base_path = malloc(strlen(path));
	strcpy(base_path, path);
	const char *name = split_path(base_path);

	struct path_info pi;
	if(get_path_info(base_path, &pi) != 0) {
		free(base_path);
		printf("get_path_info failed\n");
		return -ENOENT;
	}

	// Find and directory with specified name
	ffs_address address = {pi.directory.start_block, 0};
	struct ffs_directory directory;
	int found_directory = 0;
	for(
			uint32_t length_scanned = 0;
			length_scanned < pi.directory.length;
			length_scanned += sizeof(struct ffs_directory), address = ffs_dir_next(MOUNT_DISK, address)) {
		if(ffs_dir_read(MOUNT_DISK, address, &directory) != 0) {
			free(base_path);
			printf("ffs_dir_read failed\n");
			return -ENOENT;
		}

		if(strcmp(directory.name, name) == 0) {
			found_directory = 1;
			break;
		}
	}

	free(base_path);

	if(!found_directory) {
		printf("directory not found\n");
		return -ENOENT;
	}

	// Since this is the last link, delete directory and its data
	
	if(ffs_block_free(MOUNT_DISK, FFS_BLOCK_INVALID, directory.start_block) != 0) {
		printf("ffs_block_free failed\n");
		return -ENOENT;
	}

	if(ffs_dir_free(MOUNT_DISK, pi.address, address) != 0) {
		printf("ffs_dir_free failed\n");
		return -ENOENT;
	}

	return 0;
}

int ffs_utimens(const char *path, const struct timespec tv[2])
{
	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		FFS_ERR(2, "path info retrieval failure");
		return -1;
	}

	// Update access time
	pi.directory.access_time = time(NULL);

	if(ffs_dir_write(MOUNT_DISK, pi.address, &pi.directory) != 0) {
		return -ENOENT;
	}

	return 0;
}

int ffs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	FFS_LOG(2, "path=%s, buffer=%p, size=%zu, offset=%zd, file_info=%p", path, buffer, size, offset, file_info);

	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		FFS_ERR(2, "superblock retrieval failure");
		return -ENOENT;
	}

	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		FFS_ERR(2, "path info retrieval failure");
		return -ENOENT;
	}

	if(size + offset > pi.directory.length && resize_directory(&pi, size + offset) != 0) {
		FFS_ERR(2, "resize directory failure");
		return -ENOENT;
	}

	uint32_t first_block;
	uint32_t last_block;
	if(get_block_range(pi.directory.start_block, size, offset, &first_block, &last_block) != 0) {
		FFS_ERR(2, "block range failure");
		return -ENOENT;
	}

	// Write block by block
	while(first_block != last_block) {
		if(ffs_block_write(MOUNT_DISK, first_block, buffer) != 0) {
			FFS_ERR(2, "block write failure");
			return -ENOENT;
		}

		first_block = ffs_block_next(MOUNT_DISK, first_block);
		buffer += superblock->block_size;
	}

	return 0;
}

int make_directory(const char *path, uint32_t flags)
{
	// Need a mutable path to split it
	char *base_path = malloc(strlen(path));
	strcpy(base_path, path);
	const char *name = split_path(base_path);

	// Name too long
	if(strlen(name) > FFS_DIR_NAME_LENGTH) {
		FFS_ERR(2, "name too long");
		free(base_path);
		return -1;
	}

	struct path_info pi;
	if(get_path_info(base_path, &pi) != 0) {
		FFS_ERR(2, "path info retrieval failure");
		free(base_path);
		return -1;
	}

	// Setup directory
	time_t current_time = time(NULL);
    struct ffs_directory directory = {
		.create_time = current_time,
		.modify_time = current_time,
		.access_time = current_time,
		.length = 0,
		.start_block = FFS_BLOCK_LAST,
		.flags = flags,
		.unused = 0
    };
	strcpy(directory.name, name);

	free(base_path);

	ffs_address address = ffs_dir_alloc(MOUNT_DISK, pi.address);
	if(ffs_dir_address_valid(MOUNT_DISK, address) != 0) {
		FFS_ERR(2, "allocated address is invalid");
		return -1;
	}

	if(ffs_dir_write(MOUNT_DISK, address, &directory) != 0) {
		FFS_ERR(2, "directory write failure");
		return -1;
	}

	return 0;
}

int resize_directory(struct path_info *path_info, uint32_t length)
{
	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		return -1;
	}

	if(length < path_info->directory.length) {
		path_info->directory.length = length;

		uint32_t parent_block = FFS_BLOCK_INVALID;
		uint32_t last_block = path_info->directory.start_block;

		for(uint32_t scanned_length = 0; scanned_length < length; scanned_length += superblock->block_size) {
			parent_block = last_block;
			last_block = ffs_block_next(MOUNT_DISK, last_block);
		}

		if(ffs_block_free(MOUNT_DISK, parent_block, last_block) != 0) {
			return -1;
		}
	} else if(length > path_info->directory.length) {
		path_info->directory.length = length;

		// Get last block
		uint32_t last_block = path_info->directory.start_block;
		while(last_block != FFS_BLOCK_LAST) {
			uint32_t next_block = ffs_block_next(MOUNT_DISK, last_block);
			if(next_block == FFS_BLOCK_LAST) {
				break;
			}

			last_block = next_block;
		}

		// Capacity of the directory (ie. directory block count * block size)
		const uint32_t capacity = (1 + ((path_info->directory.length - 1) / superblock->block_size)) * superblock->block_size;

		length = safe_sub(capacity, length);

		// Need to allocate more blocks
		uint8_t *zeros = calloc(superblock->block_size, 1);
		while(length > 0) {
			FFS_LOG(2, "allocating block length=%d", length);
			last_block = ffs_block_alloc(MOUNT_DISK, last_block);
			if(last_block == FFS_BLOCK_INVALID) {
				free(zeros);
				return -1;
			}

			if(ffs_block_write(MOUNT_DISK, last_block, zeros) != 0) {
				free(zeros);
				return -1;
			}

			length = safe_sub(superblock->block_size, length);
		}

		free(zeros);
	}

	// Write updated directory
	if(ffs_dir_write(MOUNT_DISK, path_info->address, &path_info->directory) != 0) {
		return -1;
	}

	return 0;
}

uint32_t safe_sub(uint32_t a, uint32_t b)
{
	return a - (b > a ? a : b);
}

char *split_path(char *path)
{
	char *dividor = strrchr(path, '/');
	
	*dividor = '\0';
	return dividor + 1;
}
