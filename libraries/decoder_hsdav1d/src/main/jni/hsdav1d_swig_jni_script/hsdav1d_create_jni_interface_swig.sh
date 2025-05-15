#!/bin/sh

MY_DIRECTORY=$(pwd)
SWIG_JAVA_PACKAGE="androidx.media3.decoder.hsdav1d"

JNI_WRAP_JAVA_DESTINATION_PATH="$MY_DIRECTORY/../../java/androidx/media3/decoder/hsdav1d"
JNI_WRAP_JNI_DESTINATION_PATH="$MY_DIRECTORY/../hsdav1d"

##################

echo "Started creating SWIG JNI interface"
echo "JNI_WRAP_JAVA_DESTINATION_PATH: $JNI_WRAP_JAVA_DESTINATION_PATH"
echo "JNI_WRAP_JNI_DESTINATION_PATH: $JNI_WRAP_JNI_DESTINATION_PATH"

cd $MY_DIRECTORY
swig  -java -c++ -package $SWIG_JAVA_PACKAGE -outdir $JNI_WRAP_JAVA_DESTINATION_PATH -o $JNI_WRAP_JNI_DESTINATION_PATH/hsdav1d_jni_swig_generated.cpp $MY_DIRECTORY/hsdav1d_swig_interface/hsdav1d_swig_interface.i
sync

echo "Done creating SWIG JNI interface"

##################