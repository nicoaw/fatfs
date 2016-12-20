#include <ffs/ffs_aux.h>

uint32_t max_ui32(uint32_t a, uint32_t b)
{
	return a < b ? b : a;
}

uint32_t min_ui32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}
