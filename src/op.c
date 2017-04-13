#include "op.h"
#include "obj.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

// Get currently mounted disk from fuse context
#define FATFS_DISK(context)	(context->private_data)

int fatfs_chmod(const char *path, mode_t mode)
{
	syslog(LOG_DEBUG, "changing permissions for '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());

	address addr;
	struct entry ent;
	if(obj_get(d, path, &addr, &ent) != 0) {
		return -ENOENT;
	}

	// Update entry mode
	ent.mode = mode;
	if(dir_write(d, addr, &ent, sizeof(struct entry)) != sizeof(struct entry)) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "changed permissions for '%s'", path);
	return 0;
}

int fatfs_getattr(const char *path, struct stat *stats)
{
	syslog(LOG_DEBUG, "retreiving attributes for '%s'", path);

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

	stats->st_mode = ent.mode;
	stats->st_uid = context->uid;
	stats->st_gid = context->gid;
	stats->st_blksize = sb->block_size;
	stats->st_blocks = ent.size == 0 ? 0 : (ent.size - 1) / sb->block_size + 1;
	stats->st_atime = ent.access_time;
	stats->st_mtime = ent.modify_time;
	stats->st_ctime = ent.modify_time;

	if(S_ISDIR(ent.mode)) {
		// Directory is a directory
		stats->st_nlink = 2 + ent.size / sizeof(struct entry); // Count all the . and .. links
		stats->st_size = stats->st_blksize * stats->st_blocks;
	} else if(S_ISREG(ent.mode)) {
		// Directory is a file
		stats->st_nlink = 1;
		stats->st_size = ent.size;
	}

	syslog(LOG_INFO, "retreived attributes for '%s'", path);
	return 0;
}

int fatfs_mkdir(const char *path, mode_t mode)
{
	syslog(LOG_DEBUG, "creating directory '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());
	if(obj_make(d, path, mode | S_IFDIR) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "created directory '%s'", path);
	return 0;
}

int fatfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	syslog(LOG_DEBUG, "creating file '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());
	if(obj_make(d, path, mode | S_IFREG) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "created file '%s'", path);
	return 0;
}

int fatfs_open(const char *path, struct fuse_file_info *file_info)
{
	syslog(LOG_DEBUG, "opening directory '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());

	// Need entry to check if it can be opened
	struct entry ent;
	if(obj_get(d, path, NULL, &ent) != 0) {
		return -ENOENT;
	}

	// Entry is not a file
	if(!S_ISREG(ent.mode)) {
		syslog(LOG_ERR, "'%s' is not a file", path);
		return -ENOENT;
	}

	syslog(LOG_INFO, "opened directory '%s'", path);
	return 0;
}

int fatfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	syslog(LOG_DEBUG, "reading %zu bytes at offset %zd from '%s'", size, offset, path);

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
	syslog(LOG_DEBUG, "reading entries for '%s'", path);

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

int fatfs_rename(const char *oldpath, const char *newpath)
{
	syslog(LOG_DEBUG, "renaming '%s' to '%s'", oldpath, newpath);

	disk d = FATFS_DISK(fuse_get_context());

	address oldaddr;
	struct entry oldent;
	if(obj_get(d, oldpath, &oldaddr, &oldent) != 0) {
		return -ENOENT;
	}

	// Rename old entry
	const char *name = strrchr(newpath, '/') + 1;
	strcpy(oldent.name, name);

	// Remove entry at new path if it exists
	struct entry newent;
	if(obj_get(d, newpath, NULL, &newent) == 0) {
		if(S_ISDIR(newent.mode)) {
			if(!S_ISDIR(oldent.mode)) {
				return -EISDIR;
			}

			if(newent.size != 0) {
				return -ENOTEMPTY;
			}
		}

		if(S_ISDIR(oldent.mode) && !S_ISDIR(newent.mode)) {
			return -ENOTDIR;
		}

		if(obj_remove(d, newpath) != 0) {
			return -ENOENT;
		}
	}

	// Move entry from old path to new path

	if(obj_make(d, newpath, 0) != 0) {
		return -ENOENT;
	}

	address newaddr;
	if(obj_get(d, newpath, &newaddr, NULL) != 0) {
		return -ENOENT;
	}

	if(dir_write(d, newaddr, &oldent, sizeof(struct entry)) != sizeof(struct entry)) {
		return -ENOENT;
	}

	if(obj_unlink(d, oldpath) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "renamed '%s' to '%s'", oldpath, newpath);
}

int fatfs_rmdir(const char *path)
{
	syslog(LOG_DEBUG, "removing directory '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());

	if(obj_remove(d, path) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "removed directory '%s'", path);
	return 0;
}

int fatfs_truncate(const char *path, off_t size)
{
	syslog(LOG_DEBUG, "truncating '%s' to %zd bytes", path, size);

	disk d = FATFS_DISK(fuse_get_context());

	address addr;
	struct entry ent;
	if(obj_get(d, path, &addr, &ent) != 0) {
		return -ENOENT;
	}

	if(size > ent.size) {
		const uint32_t amount = size - ent.size;
		if(entry_alloc(d, addr, amount) != amount) {
			return -ENOENT;
		}
	} else if(size < ent.size) {
		const uint32_t amount = ent.size - size;
		if(entry_free(d, addr, amount) != amount) {
			return -ENOENT;
		}
	}

	syslog(LOG_INFO, "truncated '%s' to %zd bytes", path, size);
	return 0;
}

int fatfs_unlink(const char *path)
{
	syslog(LOG_DEBUG, "removing file '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());
	if(obj_remove(d, path) != 0) {
		return -ENOENT;
	}

	syslog(LOG_INFO, "removed file '%s'", path);
	return 0;
}

int fatfs_utimens(const char *path, const struct timespec tv[2])
{
	syslog(LOG_DEBUG, "updating access and modify times for '%s'", path);

	disk d = FATFS_DISK(fuse_get_context());

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

int fatfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	syslog(LOG_DEBUG, "writing %zu bytes at offset %zd from '%s'", size, offset, path);

	disk d = FATFS_DISK(fuse_get_context());

	address addr;
	struct entry ent;
	if(obj_get(d, path, &addr, &ent) != 0) {
		return -ENOENT;
	}

	// Offset needs to be positive
	if(offset < 0) {
		syslog(LOG_ERR, "invalid offset %zd", offset);
		return -ENOENT;
	}

	const uint32_t end = offset + size;

	// Need to allocate more space
	if(end > ent.size) {
		const uint32_t amount = end - ent.size;
		if(entry_alloc(d, addr, amount) != amount) {
			return -ENOENT;
		}
	}

	uint32_t wrote = entry_write(d, addr, offset, buffer, size);
	syslog(LOG_INFO, "wrote %u bytes at offset %u from '%s'", wrote, offset, path);
	return wrote;
}
