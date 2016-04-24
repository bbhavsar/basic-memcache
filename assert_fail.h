#include <assert.h>
#include <iostream>
using namespace std;

#define ASSERT(cond, msg) \
    if (!(cond)) {    \
        cerr << msg << endl;    \
        assert((cond));  \
    }

