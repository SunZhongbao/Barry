/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "bhj"
#include <android/log.h>
#include <stdio.h>
#include "jni.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#define ALOGI(...) do { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); } while(0)
#define ALOGW(...) do { __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); } while(0)
#define ALOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); } while(0)

static jboolean
checkPerm(JNIEnv *env, jobject thiz, jobject fileDescriptor) {
    struct ucred cr;
    socklen_t cr_size = sizeof(cr);

    jclass clazz = env->FindClass("java/io/FileDescriptor");
    jmethodID getFd = env->GetMethodID(clazz, "getInt$", "()I");
    jint fd = env->CallIntMethod(fileDescriptor, getFd);

    ALOGI("fd is %d\n", fd);
    /* Check socket options here */
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &cr_size) < 0) {
        return false;
    }

    if (cr.uid == 0 || cr.uid == 2000 || cr.uid == getuid()) {
        return true;
    }

    ALOGE("uid is %d, not ok\n", cr.uid);

    return false;
}

static jint
getUid(JNIEnv *env, jclass clazz) {
    jint uid = (jint)getuid();
    return uid;
}

static const char *classPathName = "com/Wrench/Input";

static JNINativeMethod methods[] = {
  {"checkPerm", "(Ljava/io/FileDescriptor;)Z", (void*)checkPerm },
  {"getUid", "()I", (void*)getUid},
};

/*
 * Register several native methods for one class.
 */
static int registerNativeMethods(JNIEnv* env, const char* className,
    JNINativeMethod* gMethods, int numMethods)
{
    jclass clazz;

    clazz = env->FindClass(className);
    if (clazz == NULL) {
        ALOGE("Native registration unable to find class '%s'", className);
        return JNI_FALSE;
    }
    if (env->RegisterNatives(clazz, gMethods, numMethods) < 0) {
        ALOGE("RegisterNatives failed for '%s'", className);
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

/*
 * Register native methods for all classes we know about.
 *
 * returns JNI_TRUE on success.
 */
static int registerNatives(JNIEnv* env)
{
  if (!registerNativeMethods(env, classPathName,
                 methods, sizeof(methods) / sizeof(methods[0]))) {
    return JNI_FALSE;
  }

  return JNI_TRUE;
}


// ----------------------------------------------------------------------------

/*
 * This is called by the VM when the shared library is first loaded.
 */

typedef union {
    JNIEnv* env;
    void* venv;
} UnionJNIEnvToVoid;

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    UnionJNIEnvToVoid uenv;
    uenv.venv = NULL;
    jint result = -1;
    JNIEnv* env = NULL;

    if (vm->GetEnv(&uenv.venv, JNI_VERSION_1_4) != JNI_OK) {
        ALOGE("ERROR: GetEnv failed");
        goto bail;
    }
    env = uenv.env;

    if (registerNatives(env) != JNI_TRUE) {
        ALOGE("ERROR: registerNatives failed");
        goto bail;
    }

    result = JNI_VERSION_1_4;

bail:
    return result;
}
