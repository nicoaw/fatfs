#ifndef FFS_OPS_H
#define FFS_OPS_H

#include <fuse/fuse.h>

// FUSE callback functions

int ffs_getattr(const char *path, struct stat *stats);

int ffs_open(const char *path, struct fuse_file_info *file_info);

int ffs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info);

int ffs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info);

#endif
