#ifndef OBJ_H
#define OBJ_H

#include "entry.h"

// Get or check existence of entry and entry address at path
// Address and entry can be NULL if they are not needed
// Returns non-zero on failure
int obj_get(disk d, const char *path, address *addr, struct entry *ent);

// Make a new object at path with specified entry flags
// Returns non-zero on failure
int obj_make(disk d, const char *path, uint32_t flags);

// Remove an object at path
// Returns non-zero on failure
int obj_remove(disk d, const char *path);

#endif
