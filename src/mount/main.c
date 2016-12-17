#define FUSE_USE_VERSION 26

#include <errno.h>
#include <ffs/ffs_dir.h>
#include <ffs/ffs_disk.h>
#include <fuse/fuse.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MOUNT_DISK ((ffs_disk) fuse_get_context()->private_data)

static int ffs_getattr(const char *path, struct stat *stats);
static int ffs_readlink(const char *path, char *, size_t);
static int ffs_getdir(const char *path, fuse_dirh_t, fuse_dirfil_t);
static int ffs_mknod(const char *path, mode_t, dev_t);
static int ffs_mkdir(const char *path, mode_t);
static int ffs_unlink(const char *path);
static int ffs_rmdir(const char *path);
static int ffs_symlink(const char *path, const char *);
static int ffs_rename(const char *path, const char *);
static int ffs_link(const char *path, const char *);
static int ffs_chmod(const char *path, mode_t);
static int ffs_chown(const char *path, uid_t, gid_t);
static int ffs_truncate(const char *path, off_t);
static int ffs_utime(const char *path, struct utimbuf *);
static int ffs_open(const char *path, struct fuse_file_info *);
static int ffs_read(const char *path, char *, size_t, off_t, struct fuse_file_info *);
static int ffs_write(const char *path, const char *, size_t, off_t, struct fuse_file_info *);
static int ffs_statfs(const char *path, struct statvfs *);
static int ffs_flush(const char *path, struct fuse_file_info *);
static int ffs_release(const char *path, struct fuse_file_info *);
static int ffs_fsync(const char *path, int, struct fuse_file_info *);
static int ffs_setxattr(const char *path, const char *, const char *, size_t, int);
static int ffs_getxattr(const char *path, const char *, char *, size_t);
static int ffs_listxattr(const char *path, char *, size_t);
static int ffs_removexattr(const char *path, const char *);
static int ffs_opendir(const char *path, struct fuse_file_info *);
static int ffs_readdir(const char *path, void *, fuse_fill_dir_t, off_t, struct fuse_file_info *);
static int ffs_releasedir(const char *path, struct fuse_file_info *);
static int ffs_fsyncdir(const char *path, int, struct fuse_file_info *);
static void *ffs_init(struct fuse_conn_info *conn);
static void ffs_destroy(void *private_data);
static int ffs_access(const char *path, int);
static int ffs_create(const char *path, mode_t, struct fuse_file_info *);
static int ffs_ftruncate(const char *path, off_t, struct fuse_file_info *);
static int ffs_fgetattr(const char *path, struct stat *, struct fuse_file_info *);
static int ffs_lock(const char *path, struct fuse_file_info *, int cmd, struct flock *);
static int ffs_utimens(const char *path, const struct timespec tv[2]);
static int ffs_bmap(const char *path, size_t blocksize, uint64_t *idx);
static int ffs_ioctl(const char *path, int cmd, void *arg, struct fuse_file_info *, unsigned int flags, void *data);
static int ffs_poll(const char *path, struct fuse_file_info *, struct fuse_pollhandle *ph, unsigned *reventsp);
static int ffs_write_buf(const char *path, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *);
static int ffs_read_buf(const char *path, struct fuse_bufvec **bufp, size_t size, off_t off, struct fuse_file_info *);
static int ffs_flock(const char *path, struct fuse_file_info *, int op);
static int ffs_fallocate(const char *path, int, off_t, off_t, struct fuse_file_info *);

int main(int argc, char** argv)
{
	struct fuse_operations operations = {
		.getattr = ffs_getattr,
		.readdir = ffs_readdir,
		.open = ffs_open,
		.read = ffs_read,
	};

	ffs_disk disk = ffs_disk_open(argv[argc - 1]);
	int result = fuse_main(argc - 1, argv, &operations, disk);

	free(disk);
	return result;
}

static int ffs_getattr(const char *path, struct stat *stats)
{
	const struct ffs_superblock *superblock = ffs_disk_superblock(MOUNT_DISK);
	if(!superblock) {
		return -ENOENT;
	}

	// Get root address
	ffs_address root_address = ffs_dir_root(MOUNT_DISK);
	if(ffs_dir_address_valid(MOUNT_DISK, root_address) != 0) {
		return -ENOENT;
	}

	// Get directory at specified path
	ffs_address address = ffs_dir_path(MOUNT_DISK, root_address, path);
	if(ffs_dir_address_valid(MOUNT_DISK, address) != 0) {
		return -ENOENT;
	}

	struct ffs_directory directory;
	if(ffs_dir_read(MOUNT_DISK, address, &directory) != 0) {
		return -ENOENT;
	}

	memset(stats, 0, sizeof(*stats));
	stats->st_mode = S_IFDIR | 0777;
	stats->st_uid = getuid();
	stats->st_gid = getgid();
	stats->st_blksize = superblock->block_size;
	stats->st_blocks = superblock->block_count;
	stats->st_atime = directory.access_time;
	stats->st_mtime = directory.modify_time;
	stats->st_ctime = directory.modify_time;

	if(directory.flags == FFS_DIR_DIRECTORY) {
		stats->st_mode |= S_IFDIR;
		stats->st_mode = S_IFDIR | 0777;
		stats->st_nlink = 2 + directory.length / sizeof(struct ffs_directory);
	} else if(directory.flags == FFS_DIR_FILE) {
		stats->st_mode |= S_IFREG;
		stats->st_nlink = 1;
		stats->st_size = directory.length;
	}

	return 0;
}

static int ffs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *file_info)
{
	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);
	return 0;
}

static int ffs_open(const char *path, struct fuse_file_info *file_info)
{
	return 0;
}

static int ffs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *file_info)
{
	return -ENOENT;
}
