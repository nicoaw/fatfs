#include "disk.h"
#include "obj.h"
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

// Split path into base path and directory name
// Path will be updated to base path
// Returns directory name offset in path
char *split_path(char *path);

int obj_get(disk d, const char *path, address *addr, struct entry *ent)
{
	syslog(LOG_DEBUG, "retreiving object '%s'", path);

	const struct superblock *sb = disk_superblock(d);

	char *mutable_path = malloc(strlen(path) + 1); // Need mutable path for strtok
	strcpy(mutable_path, path);

	address current = {sb->root_block, sizeof(struct entry)}; // Start at root address
	const char *name = strtok(mutable_path, "/");

	// Search for object entry by entry
	while(name) {
		current = entry_find(d, current, name);
		if(!DIR_ADDRESS_VALID(sb, current)) {
			free(mutable_path);
			return -1;
		}

		name = strtok(NULL, "/");
	}

	free(mutable_path);

	// Get result address when needed
	if(addr) {
		*addr = current;
	}

	// Get result entry when needed
	if(ent) {
		if(dir_read(d, current, ent, sizeof(struct entry)) != sizeof(struct entry)) {
			return -1;
		}
	}

	syslog(LOG_DEBUG, "retreived object '%s' at %u:%u", path, current.end_block, current.end_offset);
	return 0;
}

int obj_make(disk d, const char *path, uint32_t flags)
{
	syslog(LOG_DEBUG, "creating object '%s'", path);

	const struct superblock *sb = disk_superblock(d);

	char *basepath = malloc(strlen(path) + 1); // Need mutable path for split
	strcpy(basepath, path);
	const char *name = split_path(basepath);

	// Make sure name is not too long
	if(strlen(name) > ENTRY_NAME_LENGTH) {
		free(basepath);
		syslog(LOG_ERR, "object name too long '%s'", name);
		return -1;
	}

	// Need parent object to make child object
	address addr;
	struct entry parent;
	if(obj_get(d, basepath, &addr, &parent) != 0) {
		free(basepath);
		return -1;
	}

	// Allocate enough space for new object
	if(entry_alloc(d, addr, sizeof(struct entry)) != sizeof(struct entry)) {
		free(basepath);
		return -1;
	}

	// Fill entry data
	time_t t = time(NULL);
	struct entry child =
	{
    	.create_time = t,
    	.modify_time = t,
    	.access_time = t,
    	.size = 0,
    	.start_block = BLOCK_LAST,
    	.flags = flags,
    	.unused = 0,
	};
	strcpy(child.name, name);

	free(basepath);

	// Write new object
	if(entry_write(d, addr, parent.size, &child, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	syslog(LOG_DEBUG, "created object '%s'", path);
	return 0;
}

int obj_remove(disk d, const char *path)
{
	syslog(LOG_DEBUG, "removing object '%s'", path);

	address addr;
	struct entry ent;
	if(obj_get(d, path, &addr, &ent) != 0) {
		return -1;
	}

	if(entry_free(d, addr, ent.size) != ent.size) {
		return -1;
	}

	if(obj_unlink(d, path) != 0) {
		return -1;
	}

	syslog(LOG_DEBUG, "removed object '%s'", path);
}


int obj_unlink(disk d, const char *path)
{
	syslog(LOG_DEBUG, "unlinking object '%s'", path);

	const struct superblock *sb = disk_superblock(d);

	char *basepath = malloc(strlen(path) + 1); // Need mutable path for split
	strcpy(basepath, path);
	const char *name = split_path(basepath);

	// Need parent object to make child object
	address addr;
	struct entry parent;
	if(obj_get(d, basepath, &addr, &parent) != 0) {
		free(basepath);
		return -1;
	}

	// Need to move last entry to the removed one
	address removedaddr = entry_find(d, addr, name);
	address lastaddr = {parent.start_block, ENTRY_FIRST_CHUNK_SIZE(sb, parent)};

	free(basepath);

	// Read last entry
	struct entry last;
	if(dir_read(d, lastaddr, &last, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	// Write last entry at removed entry
	if(dir_write(d, removedaddr, &last, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	// Free last entry space
	if(entry_free(d, addr, sizeof(struct entry)) != sizeof(struct entry)) {
		return -1;
	}

	syslog(LOG_DEBUG, "unlinked object '%s'", path);
	return 0;
}

char *split_path(char *path)
{
	char *dividor = strrchr(path, '/');

	*dividor = '\0';
	return dividor + 1;
}
