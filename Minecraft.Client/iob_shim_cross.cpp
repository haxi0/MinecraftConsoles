#include <cstdio>

extern "C" FILE* __cdecl __acrt_iob_func(unsigned);

extern "C" FILE* __cdecl __iob_func(void)
{
	return __acrt_iob_func(0);
}
