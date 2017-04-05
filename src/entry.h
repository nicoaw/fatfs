#ifndef ENTRY_H
#define ENTRY_H

#include "dir.h"

// Maximum entry name length
#define ENTRY_NAME_LENGTH 23

// Calculate allocated size of first block
#define ENTRY_FIRST_CHUNK_SIZE(sb, ent) (ent.size == 0 ? 0 : ((ent.size - 1) % sb->block_size + 1))

// Perform only entry read access
#define entry_read(d, entry, offset, data, size) entry_access(d, entry, offset, data, NULL, size)

// Perform only entry write access
#define entry_write(d, entry, offset, data, size) entry_access(d, entry, offset, NULL, data, size)

// A directory entry
// Time fields are in seconds
struct __attribute__((__packed__)) entry {
    char name[ENTRY_NAME_LENGTH + 1];
    uint64_t create_time;
    uint64_t modify_time;
    uint64_t access_time;
    uint32_t size; // Size of directory in bytes
    uint32_t start_block; // First block in directory
    uint32_t mode; // mode_t bitset
    uint32_t unused; // Force entry structure size to be 64 bytes
};

// Allocate size bytes past end of entry
// Returns amount of bytes allocated
uint32_t entry_alloc(disk d, address entry, uint32_t size);

// Find address of child entry in entry directory
// Returns invalid address on failure
address entry_find(disk d, address entry, const char *name);

// Allocate size bytes before end of entry
// Returns amount of bytes freed
uint32_t entry_free(disk d, address entry, uint32_t size);

// Access at most size bytes of data to offset
// Stops accessing at entry end
// Returns amount of bytes accessed
uint32_t entry_access(disk d, address entry, uint32_t offset, void *readdata, const void *writedata, uint32_t size);

#endif
