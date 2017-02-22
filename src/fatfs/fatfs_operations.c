#include "fatfs_operations.h"
#include <errno.h>
#include <ffs/ffs_aux.h>
#include <ffs/ffs_block.h>
#include <ffs/ffs_dir.h>
#include <ffs/ffs_disk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MOUNT_DISK ((ffs_disk) fuse_get_context()->private_data)

/*
struct path_info
{
	ffs_address root_address;
	ffs_address address;
	struct ffs_directory directory;
};

// Get path info given specified path
// Returns zero on success
int get_path_info(const char *path, struct path_info *path_info);

// Make a file or directory on specified path
// Returns zero on success
int make_directory(const char *path, uint32_t flags);

// Split path into base path and directory name
// Path will be updated to base path
// Returns directory name offset in original path
char *split_path(char *path);

int get_path_info(const char *path, struct path_info *path_info)
{
	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		return -ENOENT;
	}

	// Get root address
	path_info->root_address = ffs_dir_root(MOUNT_DISK);
	if(!FFS_DIR_ADDRESS_VALID(path_info->root_address, superblock->block_size) != 0) {
		return -1;
	}

	// Get directory at specified path
	path_info->address = ffs_dir_path(MOUNT_DISK, path_info->root_address, path);
	if(!FFS_DIR_ADDRESS_VALID(path_info->address, superblock->block_size) != 0) {
		return -1;
	}

	if(ffs_dir_read(MOUNT_DISK, path_info->address, &path_info->directory, sizeof(struct ffs_directory)) != 0) {
		return -1;
	}

	return 0;
}

int fatfs_create(const char *path, mode_t mode, struct fuse_file_info *file_info)
{
	FFS_LOG(2, "path=%s, mode=%u, file_info=%p", path, mode, file_info);

	if(make_directory(path, FFS_DIR_FILE) != 0) {
		return -ENOENT;
	}

	return 0;
}

int fatfs_getattr(const char *path, struct stat *stats)
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

int fatfs_open(const char *path, struct fuse_file_info *file_info)
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

int fatfs_mkdir(const char *path, mode_t mode)
{
	FFS_LOG(2, "path=%s, mode=%u", path, mode);

	if(make_directory(path, FFS_DIR_DIRECTORY) != 0) {
		return -ENOENT;
	}

	return 0;
}

int fatfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	FFS_LOG(2, "path=%s, buffer=%p, size=%zu, offset=%zd, file_info=%p", path, buffer, size, offset, file_info);

	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		FFS_ERR(2, "superblock retrieval failure");
		return -EIO;
	}

	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		FFS_ERR(2, "path info retrieval failure");
		return -EIO;
	}

	const uint32_t last = ffs_min(pi.directory.length, offset + size);

	// Out of range, can't read anything
	if(offset < 0 || last == 0) {
		FFS_ERR(2, "out of range");
		return 0;
	}

	// Seek to read offset
	ffs_address address = {pi.directory.start_block, 0};
	address = ffs_dir_seek(MOUNT_DISK, address, offset);
	if(!FFS_DIR_ADDRESS_VALID(address, superblock->block_size)) {
		FFS_ERR(2, "failed to seek to offset");
		return -EIO;
	}
	
	if(ffs_dir_read(MOUNT_DISK, address, buffer, last - offset) != 0) {
		FFS_ERR(2, "failed to read buffer");
		return -EIO;
	}

	return size;
}

int fatfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info)
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
			length_scanned += sizeof(struct ffs_directory), address = ffs_dir_seek(MOUNT_DISK, address, sizeof(struct ffs_directory))) {
		struct ffs_directory directory;
		if(ffs_dir_read(MOUNT_DISK, address, &directory, sizeof(struct ffs_directory)) != 0) {
			return -ENOENT;
		}

		filler(buffer, directory.name, NULL, 0);
	}

	return 0;
}

int fatfs_truncate(const char *path, off_t size)
{
	FFS_LOG(2, "path=%s size=%d", path, size);

	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		FFS_ERR(2, "superblock retrieval failure");
		return -ENOENT;
	}

	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		return -ENOENT;
	}

	if(size < pi.directory.length) {
		// Get offset address to free from
		ffs_address offset_address = {pi.directory.start_block, 0};
		offset_address = ffs_dir_seek(MOUNT_DISK, offset_address, pi.directory.length - size);
		if(!FFS_DIR_ADDRESS_VALID(offset_address, superblock->block_size)) {
			return -ENOENT;
		}

		// Free space at back of directory
		if(ffs_dir_free(MOUNT_DISK, pi.address, offset_address, size) != 0) {
			return -ENOENT;
		}
	} else {
		// Allocate space at back of directory
		ffs_address address = ffs_dir_alloc(MOUNT_DISK, pi.address, size - pi.directory.length);
		if(!FFS_DIR_ADDRESS_VALID(address, superblock->block_size)) {
			return -ENOENT;
		}
	}
	
	return 0;
}

int fatfs_unlink(const char *path)
{
	FFS_LOG(2, "path=%s", path);

	// Need a mutable path to split it
	char *base_path = malloc(strlen(path));
	strcpy(base_path, path);
	const char *name = split_path(base_path);

	struct path_info base_pi;
	if(get_path_info(base_path, &base_pi) != 0) {
		free(base_path);
		FFS_ERR(2, "base path info retrieval failure");
		return -ENOENT;
	}

	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		free(base_path);
		FFS_ERR(2, "path info retrieval failure");
		return -ENOENT;
	}

	free(base_path);

	// Since this is the last link, delete directory and its data
	if(ffs_block_free(MOUNT_DISK, FFS_BLOCK_INVALID, pi.directory.start_block) != 0) {
		FFS_ERR(2, "ffs_block_free failed");
		return -ENOENT;
	}

	if(ffs_dir_free(MOUNT_DISK, base_pi.address, pi.address, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(2, "ffs_dir_free failed");
		return -ENOENT;
	}

	return 0;
}

int fatfs_utimens(const char *path, const struct timespec tv[2])
{
	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		FFS_ERR(2, "path info retrieval failure");
		return -1;
	}

	// Update access time
	pi.directory.access_time = time(NULL);

	if(ffs_dir_write(MOUNT_DISK, pi.address, &pi.directory, sizeof(struct ffs_directory)) != 0) {
		return -ENOENT;
	}

	return 0;
}

int fatfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	FFS_LOG(2, "path=%s, buffer=%p, size=%zu, offset=%zd, file_info=%p", path, buffer, size, offset, file_info);

	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		FFS_ERR(2, "superblock retrieval failure");
		return -EIO;
	}

	struct path_info pi;
	if(get_path_info(path, &pi) != 0) {
		FFS_ERR(2, "path info retrieval failure");
		return -EIO;
	}

	if(offset < 0)
	{
		FFS_ERR(2, "offset out of range");
		return 0;
	}

	const size_t last = size + offset;

	// Need to allocate more space for directory
	if(last > pi.directory.length) {
		ffs_address address = ffs_dir_alloc(MOUNT_DISK, pi.address, last - pi.directory.length);
		if(!FFS_DIR_ADDRESS_VALID(address, FFS_DIR_OFFSET_INVALID)) {
			FFS_ERR(2, "allocated address invalid");
			return -EIO;
		}

		// Update directory information (if start block is modified)
		if(ffs_dir_read(MOUNT_DISK, pi.address, &pi.directory, sizeof(struct ffs_directory)) != 0) {
			FFS_ERR(2, "failed to update directory");
			return -EIO;
		}
	}

	// Seek to write offset
	ffs_address address = {pi.directory.start_block, 0};
	FFS_LOG(2, "address={block=%d offset=%u}", address.block, address.offset);
	address = ffs_dir_seek(MOUNT_DISK, address, offset);
	if(!FFS_DIR_ADDRESS_VALID(address, superblock->block_size)) {
		FFS_ERR(2, "failed to seek to offset");
		return -EIO;
	}
	
	if(ffs_dir_write(MOUNT_DISK, address, buffer, size) != 0) {
		FFS_ERR(2, "failed to write buffer");
		return -EIO;
	}

	return size;
}

//TODO: somehow can't make more than three directories per parent directory
//		maybe block boundaries are cause
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

	ffs_address address = ffs_dir_alloc(MOUNT_DISK, pi.address, sizeof(struct ffs_directory));
	if(!FFS_DIR_ADDRESS_VALID(address, FFS_DIR_OFFSET_INVALID)) {
		FFS_ERR(2, "allocated address is invalid");
		return -1;
	}

	if(ffs_dir_write(MOUNT_DISK, address, &directory, sizeof(struct ffs_directory)) != 0) {
		FFS_ERR(2, "directory write failure");
		return -1;
	}

	return 0;
}

char *split_path(char *path)
{
	char *dividor = strrchr(path, '/');
	
	*dividor = '\0';
	return dividor + 1;
}
*/
