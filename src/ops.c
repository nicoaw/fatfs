#include "ops.h"
#include "obj.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

// Get currently mounted disk from fuse context
#define FATFS_DISK(context)	(context->private_data)

int fatfs_getattr(const char *path, struct stat *stats)
{
	syslog(LOG_INFO, "retreiving attributes for '%s'", path);

	struct fuse_context *context = fuse_get_context();
	disk d = FATFS_DISK(fuse_get_context());
	const struct superblock *sb = disk_superblock(d);

	// Need entry data
	struct entry ent;
	if(obj_get(d, path, NULL, &ent) != 0) {
		return -ENOENT;
	}

	// Initially clear stats
	memset(stats, 0, sizeof(*stats));

	stats->st_mode = S_IFDIR | 0777;
	stats->st_uid = context->uid;
	stats->st_gid = context->gid;
	stats->st_blksize = sb->block_size;
	stats->st_atime = ent.access_time;
	stats->st_mtime = ent.modify_time;
	stats->st_ctime = ent.modify_time;

	// Count blocks allocated to file
	block block = ent.start_block;
	while(block != BLOCK_LAST) {
		++stats->st_blocks;
		block = block_next(d, block);
		if(block == BLOCK_INVALID) {
			return -ENOENT;
		}
	}

	if(ent.flags & ENTRY_DIRECTORY) {
		// Directory is a directory
		stats->st_mode = S_IFDIR | 0555;
		stats->st_nlink = 2 + ent.size / sizeof(struct entry); // Count all the . and .. links
	} else if(ent.flags & ENTRY_FILE) {
		// Directory is a file
		stats->st_mode = S_IFREG | 0666;
		stats->st_nlink = 1;
		stats->st_size = ent.size;
	}

	syslog(LOG_INFO, "retreived attributes for '%s'", path);
	return 0;
}

int fatfs_mkdir(const char *path, mode_t mode)
{
	syslog(LOG_INFO, "creating directory '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());
	if(obj_make(d, path, ENTRY_DIRECTORY) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "created directory '%s'", path);
	return 0;
}

int fatfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	syslog(LOG_INFO, "creating file '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());
	if(obj_make(d, path, ENTRY_FILE) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "created file '%s'", path);
	return 0;
}

int fatfs_open(const char *path, struct fuse_file_info *file_info)
{
	syslog(LOG_INFO, "opening directory '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());

	// Need entry to check if it can be opened
	struct entry ent;
	if(obj_get(d, path, NULL, &ent) != 0) {
		return -ENOENT;
	}

	// Entry is not a file
	if(!(ent.flags & ENTRY_FILE)) {
		syslog(LOG_ERR, "'%s' is not a file", path);
		return -ENOENT;
	}

	syslog(LOG_INFO, "opened directory '%s'", path);
	return 0;
}

int fatfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	syslog(LOG_INFO, "reading %zu bytes at offset %zd from '%s'", size, offset, path);

	disk d = FATFS_DISK(fuse_get_context());

	address addr;
	if(obj_get(d, path, &addr, NULL) != 0) {
		return -ENOENT;
	}

	// Offset needs to be positive
	if(offset < 0) {
		syslog(LOG_ERR, "invalid offset %zd", offset);
		return -ENOENT;
	}

	uint32_t read = entry_read(d, addr, offset, buffer, size);
	memset(buffer + read, 0, size - read); // Zero the untouched part of buffer
	syslog(LOG_INFO, "read %u bytes at offset %u from '%s'", read, offset, path);
	return read;
}

int fatfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info)
{
	syslog(LOG_INFO, "reading entries for '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());

	// Fill generated links
	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	address addr;
	struct entry parent;
	if(obj_get(d, path, &addr, &parent) != 0) {
		return -ENOENT;
	}

	struct entry *children = malloc(parent.size);
	if(entry_read(d, addr, 0, children, parent.size) != parent.size) {
		free(children);
		return -ENOENT;
	}

	for(uint32_t i = 0; i < parent.size / sizeof(struct entry); ++i) {
		filler(buffer, children[i].name, NULL, 0);
	}

	free(children);
	syslog(LOG_INFO, "read entries for '%s'", path);
	return 0;
}

int fatfs_rmdir(const char *path)
{
	syslog(LOG_INFO, "removing directory '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());
	if(obj_remove(d, path) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "removed directory '%s'", path);
	return 0;
}

int fatfs_unlink(const char *path)
{
	syslog(LOG_INFO, "removing file '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());
	if(obj_remove(d, path) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "removed file '%s'", path);
	return 0;
}

int fatfs_utimens(const char *path, const struct timespec tv[2])
{
	syslog(LOG_INFO, "updating access and modify times for '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());
	const struct superblock *sb = disk_superblock(d);

	address addr;
	struct entry ent;
	if(obj_get(d, path, &addr, &ent) != 0) {
		return -ENOENT;
	}

	// Update access and modify times
	ent.access_time = tv[0].tv_sec;
	ent.modify_time = tv[1].tv_sec;

	// Write changes
	if(dir_write(d, addr, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "updated access and modify times for '%s'", path);
	return 0;
}
