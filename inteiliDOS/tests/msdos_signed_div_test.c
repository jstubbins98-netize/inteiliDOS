/* Host-side regression for the freestanding helper used by 32-bit IDIV. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>

extern long long __divmoddi4(long long, long long, long long *);

int main(void) {
    const long long values[] = {
        LLONG_MIN, LLONG_MIN+1, -4294967297LL, -65537, -1, 0,
        1, 65537, 4294967297LL, LLONG_MAX
    };
    for (unsigned i=0;i<sizeof values/sizeof *values;i++)
        for (unsigned j=0;j<sizeof values/sizeof *values;j++) {
            volatile long long n=values[i],d=values[j];
            if (!d || (n==LLONG_MIN && d==-1)) continue;
            long long remainder=0;
            long long quotient=__divmoddi4(n,d,&remainder);
            assert(quotient==n/d);
            assert(remainder==n%d);
        }
    puts("PASS: signed 64-bit quotient/remainder helper for 32-bit guest IDIV");
}
