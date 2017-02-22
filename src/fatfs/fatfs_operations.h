#ifndef FFS_OPS_H
#define FFS_OPS_H

#include <fuse/fuse.h>

// FUSE callback functions

int ffs_create(const char *path, mode_t mode, struct fuse_file_info *file_info);

int ffs_getattr(const char *path, struct stat *stats);

int ffs_open(const char *path, struct fuse_file_info *file_info);

int ffs_mkdir(const char *path, mode_t mode);

int ffs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info);

int ffs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info);

int ffs_truncate(const char *path, off_t size);

int ffs_unlink(const char *path);

int ffs_utimens(const char *path, const struct timespec tv[2]);

int ffs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info);

#endif
