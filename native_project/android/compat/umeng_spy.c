// The original APK puts a 32-bit analytics fingerprint library in arm64-v8a.
// Offline play does not need device profiling. Preserve its two JNI methods
// with an ARM64 implementation that reports an unavailable identifier.
#include <jni.h>
JNIEXPORT jstring JNICALL Java_com_umeng_umzid_Spy_getNativeID(JNIEnv* env, jclass cls) {
    (void)cls; return (*env)->NewStringUTF(env, "");
}
JNIEXPORT jstring JNICALL Java_com_umeng_umzid_Spy_getNativeLibraryVersion(JNIEnv* env, jclass cls) {
    (void)cls; return (*env)->NewStringUTF(env, "offline");
}
