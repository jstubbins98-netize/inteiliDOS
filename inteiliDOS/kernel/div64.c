/* Freestanding GCC unsigned 64-bit division ABI for exFAT/GPT.
 * Shift/subtract only: no FPU, hosted libc, or recursive compiler helpers. */
typedef unsigned long long u64;
static u64 divide(u64 n,u64 d,u64 *remainder) {
    u64 q=0,r=0;
    if(!d)__builtin_trap();
    for(int bit=63;bit>=0;bit--){
        unsigned carry=(unsigned)(r>>63);
        r=(r<<1)|((n>>bit)&1);
        if(carry||r>=d){r-=d;q|=(u64)1<<bit;}
    }
    if(remainder)*remainder=r;
    return q;
}
u64 __udivdi3(u64 n,u64 d){return divide(n,d,0);}
u64 __umoddi3(u64 n,u64 d){u64 r;divide(n,d,&r);return r;}
u64 __udivmoddi4(u64 n,u64 d,u64 *r){return divide(n,d,r);}
typedef long long s64;
s64 __divmoddi4(s64 n,s64 d,s64 *remainder) {
    u64 un=n<0?0-(u64)n:(u64)n,ud=d<0?0-(u64)d:(u64)d,r;
    u64 q=divide(un,ud,&r);
    if (remainder) *remainder=(s64)(n<0?0-r:r);
    return (s64)((n<0)!=(d<0)?0-q:q);
}
