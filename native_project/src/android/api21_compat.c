#include <stdio.h>
#include <pthread.h>
// Frida's prebuilt archive refers to API 23 symbols; bind them locally.
#if __ANDROID_API__ < 23
#undef stdout
#undef stderr
__attribute__((visibility("hidden"))) FILE* stdout = &__sF[1];
__attribute__((visibility("hidden"))) FILE* stderr = &__sF[2];
__attribute__((visibility("hidden"))) int __register_atfork(
    void (*prepare)(void), void (*parent)(void), void (*child)(void), void* dso) {
    (void)dso; // The embedded archive lives for the entire process.
    return pthread_atfork(prepare, parent, child);
}
#endif
