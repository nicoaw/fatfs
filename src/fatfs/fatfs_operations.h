#ifndef FATFS_OPERATIONS_H
#define FATFS_OPERATIONS_H

#include <fuse/fuse.h>

// FUSE callback functions

int fatfs_create(const char *path, mode_t mode, struct fuse_file_info *file_info);

int fatfs_getattr(const char *path, struct stat *stats);

int fatfs_open(const char *path, struct fuse_file_info *file_info);

int fatfs_mkdir(const char *path, mode_t mode);

int fatfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info);

int fatfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info);

int fatfs_truncate(const char *path, off_t size);

int fatfs_unlink(const char *path);

int fatfs_utimens(const char *path, const struct timespec tv[2]);

int fatfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info);

#endif
