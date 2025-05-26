
#include <GLES2/gl2.h>
#include <EGL/egl.h>

// Compile and link shader program
GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexSource, nullptr);
  glCompileShader(vertexShader);

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
  glCompileShader(fragmentShader);

  GLuint program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return program;
}

// Vertex shader (standard for 2D rendering)
const char* vertexShaderSource = R"(
    attribute vec4 aPosition;
    attribute vec2 aTexCoord;
    varying vec2 vTexCoord;
    void main() {
        gl_Position = aPosition;
        vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y); // Flip vertically
    }
)";

// Fragment shader for YUV to RGB conversion
const char* fragmentShaderSource = R"(
    precision mediump float;
    varying vec2 vTexCoord;
    uniform sampler2D yTexture;
    uniform sampler2D uTexture;
    uniform sampler2D vTexture;

    void main() {
        float y = texture2D(yTexture, vTexCoord).r;
        float u = texture2D(uTexture, vTexCoord).r - 0.5;
        float v = texture2D(vTexture, vTexCoord).r - 0.5;

        float r = y + 1.402 * v;
        float g = y - 0.344 * u - 0.714 * v;
        float b = y + 1.772 * u;

        gl_FragColor = vec4(r, g, b, 1.0);
    }
)";

// Function to render YUV frame to surface using OpenGL
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
                           int vStride) {
  // Set up EGL to bind the surface and OpenGL context
  eglMakeCurrent(display, surface, surface, eglGetCurrentContext());

//  // Compile shaders and create program
//  GLuint program = createProgram(vertexShaderSource, fragmentShaderSource);
  glUseProgram(program);

  // Generate and bind textures for Y, U, V planes
  GLuint textures[3];
  glGenTextures(3, textures);

// Bind Y texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textures[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

// Upload Y plane row by row
  for (int row = 0; row < height; ++row) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, width, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, yBuffer + row * yStride);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

// Bind U texture (half resolution)
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, textures[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width / 2, height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

// Upload U plane row by row
  for (int row = 0; row < height / 2; ++row) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, width / 2, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, uBuffer + row * uStride);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

// Bind V texture (half resolution)
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, textures[2]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width / 2, height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);

// Upload V plane row by row
  for (int row = 0; row < height / 2; ++row) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, width / 2, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, vBuffer + row * vStride);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Set the texture uniforms in the shader
  GLint yTexLocation = glGetUniformLocation(program, "yTexture");
  GLint uTexLocation = glGetUniformLocation(program, "uTexture");
  GLint vTexLocation = glGetUniformLocation(program, "vTexture");
  glUniform1i(yTexLocation, 0); // GL_TEXTURE0
  glUniform1i(uTexLocation, 1); // GL_TEXTURE1
  glUniform1i(vTexLocation, 2); // GL_TEXTURE2

  // Define vertices for a fullscreen quad
  const GLfloat vertices[] = {
      // X, Y, S, T (position and texture coordinates)
      -1.0f, -1.0f, 0.0f, 0.0f,
      1.0f, -1.0f, 1.0f, 0.0f,
      -1.0f,  1.0f, 0.0f, 1.0f,
      1.0f,  1.0f, 1.0f, 1.0f,
  };

  // Create and bind vertex buffer
  GLuint vertexBuffer;
  glGenBuffers(1, &vertexBuffer);
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  // Set up vertex attributes
  GLint positionLocation = glGetAttribLocation(program, "aPosition");
  GLint texCoordLocation = glGetAttribLocation(program, "aTexCoord");
  glEnableVertexAttribArray(positionLocation);
  glVertexAttribPointer(positionLocation, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
  glEnableVertexAttribArray(texCoordLocation);
  glVertexAttribPointer(texCoordLocation, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));

  // Clear the surface
  glClear(GL_COLOR_BUFFER_BIT);

  // Draw the quad (rendering the textures)
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glDeleteBuffers(1, &vertexBuffer);
  glDeleteTextures(3, textures);
  //glDeleteProgram(program);

  // Swap buffers to display the rendered frame
  eglSwapBuffers(display, surface);
}

// Deinitialize
void deInitializeEGL(EGLDisplay eglDisplay,
                     EGLSurface eglSurface,
                     EGLContext eglContext,
                     GLuint eglProgram) {
  glDeleteProgram(eglProgram);
  eglDestroyContext(eglDisplay, eglContext);
  eglDestroySurface(eglDisplay, eglSurface);
}

// Initialize EGL and get EGLDisplay
bool initializeEGL(ANativeWindow *window,
                   EGLSurface *eglSurface,
                   EGLContext *eglContext,
                   EGLDisplay *eglDisplay,
                   GLuint *eglProgram) {
  // 1. Get the default display
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY) {
    // Handle error: no display found
    return false;
  }
  *eglDisplay = display;

  // 2. Initialize EGL
  if (!eglInitialize(display, nullptr, nullptr)) {
    // Handle error: EGL initialization failed
    return false;
  }

  // 3. Configure EGL attributes
  const EGLint attribs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_BLUE_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_RED_SIZE, 8,
      EGL_DEPTH_SIZE, 16,
      EGL_NONE
  };

  EGLConfig config;
  EGLint numConfigs;
  if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)) {
    // Handle error: failed to choose config
    return false;
  }

  // 4. Create an EGL window surface
  *eglSurface = eglCreateWindowSurface(display, config, window, nullptr);
  if (*eglSurface == EGL_NO_SURFACE) {
    // Handle error: failed to create surface
    return false;
  }

  // 5. Create an EGL rendering context
  const EGLint contextAttribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,  // We are using OpenGL ES 2.0
      EGL_NONE
  };

  *eglContext = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
  if (*eglContext == EGL_NO_CONTEXT) {
    // Handle error: failed to create context
    return false;
  }

  // 6. Make the EGL context current
  if (!eglMakeCurrent(display, *eglSurface, *eglSurface, *eglContext)) {
    // Handle error: failed to make context current
    return false;
  }

  // Compile shaders and create program
  *eglProgram = createProgram(vertexShaderSource, fragmentShaderSource);

  return true;
}