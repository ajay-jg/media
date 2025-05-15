%module "HsDavidSwigGenerated"

%{

#include <stdarg.h>
#include <jni.h>
#include "hsdav1d_wrapper.h"

extern void set_jvm_pointer(JavaVM *jvm);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  (void)reserved;

  set_jvm_pointer(vm);
  return JNI_VERSION_1_6;
}

%}

// Typemap to handle context - Start
%typemap(jtype) (void*) "long"
%typemap(jstype) (void*) "long"
%typemap(jni) (void*) "jlong"
%typemap(javain) (void*) "$javainput"
%typemap(javaout) (void*) {
  return $jnicall;
}
%typemap(javadestruct) (void*) ""
%typemap(out) (void*) "$result = (jlong) (intptr_t) $1;"
// Typemap to handle context - End

// Typemap to handle Java ByteBuffer - Start
%typemap(jtype) (const unsigned char* buffer) "java.nio.ByteBuffer"
%typemap(jstype) (const unsigned char* buffer) "java.nio.ByteBuffer"
%typemap(jni) (const unsigned char* buffer) "jobject"
%typemap(javain) (const unsigned char* buffer) "$javainput"
%typemap(in) (const unsigned char* buffer) %{
// Cast the jobject to ByteBuffer
jobject byteBuffer = $input;
if (!byteBuffer) {
SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "ByteBuffer must not be null");
return $null;
}

$1 = (unsigned char*)jenv->GetDirectBufferAddress(byteBuffer);
if (!$1) {
// Handle non-direct ByteBuffer if necessary
jbyteArray array = (jbyteArray)jenv->CallObjectMethod(byteBuffer, jenv->GetMethodID(jenv->GetObjectClass(byteBuffer), "array", "()[B"));
$1 = (unsigned char*)jenv->GetByteArrayElements(array, 0);
}

%}
%typemap(javacall) (const unsigned char* buffer) "$javainput"
%typemap(javadestruct) (const unsigned char* buffer) %{
jobject byteBuffer = $input;
$1 = (unsigned char*)jenv->GetDirectBufferAddress(byteBuffer);
if (!$1) {
jbyteArray array = (jbyteArray)jenv->CallObjectMethod(byteBuffer, jenv->GetMethodID(jenv->GetObjectClass($input), "array", "()[B"));
jenv->ReleaseByteArrayElements(array, (jbyte*)$1, 0);
}
%}
// Typemap to handle Java ByteBuffer - End

// Typemap to handle output buffer - Start
%typemap(jtype) (jobject jOutputBuffer) "androidx.media3.decoder.VideoDecoderOutputBuffer"
%typemap(jstype) (jobject jOutputBuffer) "androidx.media3.decoder.VideoDecoderOutputBuffer"
%typemap(jni) (jobject jOutputBuffer) "jobject"
%typemap(javain) (jobject jOutputBuffer) "$javainput"
%typemap(javacall) (jobject jOutputBuffer) "$javainput"
%typemap(javadestruct) (jobject jOutputBuffer) { }
// Typemap to handle output buffer - End

// Typemap to handle Surface - Start
%typemap(jtype) (jobject jSurface) "android.view.Surface"
%typemap(jstype) (jobject jSurface) "android.view.Surface"
%typemap(jni) (jobject callback) "jobject"
%typemap(javain) (jobject jSurface) "$javainput"
%typemap(javacall) (jobject jSurface) "$javainput"
%typemap(javadestruct) (jobject jSurface) {}
// Typemap to handle Surface - End

// Typemap to handle callback - Start
%typemap(jtype) (jobject callback) "HsDav1dJniLogger"
%typemap(jstype) (jobject callback) "HsDav1dJniLogger"
%typemap(jni) (jobject callback) "jobject"
%typemap(javain) (jobject callback) "$javainput"
%typemap(javacall) (jobject callback) "$javainput"
%typemap(javadestruct) (jobject callback) {}
// Typemap to handle callback - End


// Methods called from Java.
%include "../../hsdav1d/hsdav1d_wrapper.h"