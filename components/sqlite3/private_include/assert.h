#ifndef __DUMMY_ASSERT_H
#define __DUMMY_ASSERT_H
#ifdef NDEBUG
#define assert(x) 
#else
#include_next <assert.h>
#endif
#endif

