#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

// Short description:
// this application moves fs register value to variable dst0.
// so the regular execution should see dst0 changed from -1
// to something different than -1. A mov instruction is used.
// when the tool addrtranslate.so is used - the address is changed
// to &dst1 such that now dst0 is unchanged and dst1 is changed.
// this is the final check below.

int main()
{
    void* dst[2] = { (void*)-1, (void*)-1 };
    void* val = (void*)&dst[0];

#ifdef TARGET_IA32E
    __asm__ volatile ( "movq %0, %%rax" :: "r"(val): "memory" );
    __asm__ volatile ("fnop");
    __asm__ volatile ("movw %%fs, (%%rax)" ::: "%rax");
#else
    __asm__ volatile ( "mov %0, %%eax" :: "r"(val): "memory" );
    __asm__ volatile ("fnop");
    __asm__ volatile ("movw %%fs, (%%eax)" ::: "%eax");
#endif

    printf("dst[0]=%p dst[1]=%p\n", dst[0], dst[1]);

    // if dst0 was changed then address translation did not work
    if (dst[0] != (void*)-1)
    {
        return 1;
    }
    
    // and reversely if dst1 was not changed then address translation
    // did not work as well
    if (dst[1] == (void*)-1)
    {
        return 2;
    }

    return 0;
}
