#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned long long u64;
u64 __udivdi3(u64,u64);
u64 __umoddi3(u64,u64);
u64 __udivmoddi4(u64,u64,u64*);
int main(void) {
    const u64 cases[]={0,1,2,3,0xFFFFFFFFULL,0x100000000ULL,
        0x8000000000000000ULL,0xFFFFFFFFFFFFFFFFULL};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)
        for(unsigned j=1;j<sizeof(cases)/sizeof(*cases);j++){
            u64 r,q=__udivmoddi4(cases[i],cases[j],&r);
            assert(q==cases[i]/cases[j]&&r==cases[i]%cases[j]);
        }
    u64 state=1967;
    for(int i=0;i<30000;i++){
        state=state*6364136223846793005ULL+1;u64 a=state;
        state=state*6364136223846793005ULL+1;u64 b=state|1;
        assert(__udivdi3(a,b)==a/b);assert(__umoddi3(a,b)==a%b);
    }
    puts("PASS: unsigned 64-bit division/remainder edge cases and 30000 vectors");
}
