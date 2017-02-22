#include <ffs/ffs_aux.h>

uint32_t ffs_max(uint32_t a, uint32_t b)
{
	return a < b ? b : a;
}

uint32_t ffs_min(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}
