//
// Created by Mayur K on 13/10/24.
//

#ifndef EXOPLAYER_HSDAV1D_OPENGL_RENDER_UTILS_H
#define EXOPLAYER_HSDAV1D_OPENGL_RENDER_UTILS_H

#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
bool initializeEGL(ANativeWindow *window,
                   EGLSurface *eglSurface,
                   EGLContext *eglContext,
                   EGLDisplay *eglDisplay,
                   GLuint *eglProgram);

void deInitializeEGL(EGLDisplay eglDisplay,
                     EGLSurface eglSurface,
                     EGLContext eglContext,
                     GLuint eglProgram);

void renderYUV420ToSurface(EGLSurface surface,
                           EGLDisplay display,
                           GLuint program,
                           int width,
                           int height,
                           const uint8_t* yBuffer,
                           const uint8_t* uBuffer,
                           const uint8_t* vBuffer,
                           int yStride,
                           int uStride,
                           int vStride);

#endif //EXOPLAYER_HSDAV1D_OPENGL_RENDER_UTILS_H