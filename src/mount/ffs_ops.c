#include "ffs_ops.h"

#include <errno.h>
#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>
#include <ffs/ffs_disk.h>
#include <string.h>
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

int ffs_getattr(const char *path, struct stat *stats)
{
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
		stats->st_mode |= S_IFDIR;
		stats->st_mode = S_IFDIR | 0777;
		stats->st_nlink = 2 + pi.directory.length / sizeof(struct ffs_directory);
	} else if(pi.directory.flags == FFS_DIR_FILE) {
		stats->st_mode |= S_IFREG;
		stats->st_nlink = 1;
		stats->st_size = pi.directory.length;
	}

	return 0;
}

int ffs_open(const char *path, struct fuse_file_info *file_info)
{
	// Need to check if file exists
	if(!(file_info->flags & O_CREAT)) {
		struct path_info pi;
		if(get_path_info(path, &pi) != 0) {
			return -ENOENT;
		}
	}

	return 0;
}

int ffs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
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
