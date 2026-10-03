/*
 * Host compatibility shim for the C ABI shared libraries.
 *
 * gcc 11+ emits references to __libc_single_threaded (glibc >= 2.32 only)
 * from shared_ptr/<atomic> single-threaded fast paths, and gcc 12's
 * libstdc++ pulls in std::random_device which calls arc4random (glibc
 * >= 2.36 only). On older hosts (e.g. ubuntu 20.04, glibc 2.31) those
 * symbols do not exist and the library would fail to load, so provide
 * copies here. arc4random stays exported: the reference comes from
 * libstdc++.so itself and resolves in the global scope. The
 * __libc_single_threaded copy is visibility("hidden") -- its references
 * are inlined into this library's own objects, and a default-visibility
 * exported data symbol could preempt libc's real per-process value in a
 * multithreaded host.
 */

#include <sys/random.h>

__attribute__((visibility("hidden"))) int __libc_single_threaded = 1;

unsigned int arc4random(void)
{
    /* single-threaded fallback for glibc < 2.36; libc's copy wins on
     * newer hosts. std::random_device is the only caller. */
    unsigned int value = 0;
    if (getentropy(&value, sizeof(value)) == 0)
    {
        return value;
    }
    return 0;
}
