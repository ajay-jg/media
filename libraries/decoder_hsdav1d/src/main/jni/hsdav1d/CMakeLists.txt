#
# Copyright 2021 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.7.1 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 11)

project(libhsdav1dJNI C CXX)

# Devices using armeabi-v7a are not required to support
# Neon which is why Neon is disabled by default for
# armeabi-v7a build. This flag enables it.
if(${ANDROID_ABI} MATCHES "armeabi-v7a")
    add_compile_options("-mfpu=neon")
    add_compile_options("-marm")
    add_compile_options("-fPIC")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type)
if(build_type MATCHES "^rel")
    add_compile_options("-O2")
endif()

set(libhsdav1d_jni_root "${CMAKE_CURRENT_SOURCE_DIR}")

# Set the Android ABIs
set(ANDROID_ABIS "armeabi-v7a" "arm64-v8a" "x86" "x86_64")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/../)

# Print the values of the CMake variables
message(WARNING "DAV1D_LIB_DEPENDENCY_DIR:  ${DAV1D_LIB_DEPENDENCY_DIR}")

# Check if the variables are set
if(NOT DEFINED DAV1D_LIB_DEPENDENCY_DIR)
    message(WARNING "Variable DAV1D_LIB_DEPENDENCY_DIR is not defined")
endif()

# Build libhsdav1dJNI.
add_library(hsdav1dJNI
        SHARED
        hsdav1d_jni_swig_generated.cpp
        hsdav1d_wrapper.cpp
        hsdav1d_wrapper.h)

# Link the appropriate library based on the ABI
foreach(ABI ${ANDROID_ABIS})
    message(WARNING "Including path ${DAV1D_LIB_DEPENDENCY_DIR}/${ABI}/include/")
    include_directories(${DAV1D_LIB_DEPENDENCY_DIR}/${ABI}/include/)
    add_library(Dav1dLib_${ABI} SHARED IMPORTED)
    set_target_properties(Dav1dLib_${ABI} PROPERTIES IMPORTED_LOCATION
            ${DAV1D_LIB_DEPENDENCY_DIR}/${ABI}/lib//libdav1d.so)
    set_target_properties(Dav1dLib_${ABI} PROPERTIES MAP_IMPORTED_CONFIG_RELWITHDEBINFO NOCONFIG)

    if(ANDROID_ABI STREQUAL ${ABI})
        target_link_libraries(hsdav1dJNI
                PRIVATE Dav1dLib_${ABI}
                PRIVATE android)
    endif()
endforeach()