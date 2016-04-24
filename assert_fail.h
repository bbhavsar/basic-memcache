#include <assert.h>
#include <stdio.h>

#define ASSERT(cond, msg) \
    if (!(cond)) {    \
        fprintf(stderr, "%s\n", msg);    \
        assert((cond));  \
    }

