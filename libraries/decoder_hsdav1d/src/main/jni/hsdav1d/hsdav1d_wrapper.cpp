
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "picture.h"
#include "data.h"
#include "dav1d.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <stdarg.h>
#include <jni.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>

#include "hsdav1d_wrapper.h"

void log_callback(void *context, int logLevel, const char* message);

JavaVM *gHsDav1dJvm;

#define ENABLE_DEBUG_LOG (0)
#define ENABLE_INFO_LOG  (1)
#define ENABLE_WARN_LOG  (1)
#define ENABLE_ERROR_LOG (1)


#if ENABLE_DEBUG_LOG
#define LOG_DEBUG(context, format, ...) \
    {                                         \
        char str[128];                        \
        snprintf(str, 127, format, ##__VA_ARGS__);  \
        log_callback(context, kHsDavidLogLevelDebug, str); \
    }
#else
#define LOG_DEBUG(context, format, ...) {}
#endif

#if ENABLE_INFO_LOG
#define LOG_INFO(context, format, ...) \
    {                                         \
        char str[128];                        \
        snprintf(str, 127, format, ##__VA_ARGS__);  \
        log_callback(context, kHsDavidLogLevelInformation, str); \
    }
#else
#define LOG_INFO(context, format, ...) {}
#endif

#if ENABLE_WARN_LOG
#define LOG_WARN(context, format, ...) \
    {                                         \
        char str[128];                        \
        snprintf(str, 127, format, ##__VA_ARGS__);  \
        log_callback(context, kHsDavidLogLevelWarning, str); \
    }
#else
#define LOG_WARN(context, format, ...) {}
#endif

#if ENABLE_ERROR_LOG
#define LOG_ERROR(context, format, ...) \
    {                                         \
        char str[128];                        \
        snprintf(str, 127, format, ##__VA_ARGS__);  \
        log_callback(context, kHsDavidLogLevelError, str); \
    }
#else
#define LOG_ERROR(context, format, ...) {}
#endif

namespace {

const int kPlaneY = 0;
const int kPlaneU = 1;
const int kPlaneV = 2;
const int kMaxPlanes = 3;


// Output modes.
const int kOutputModeYuv = 0;
const int kOutputModeSurfaceYuv = 1;

const int kColorSpaceUnknown = 0;

// Android YUV format. See:
// https://developer.android.com/reference/android/graphics/ImageFormat.html#YV12.
const int kImageFormatYV12 = 0x32315659;

// Return codes for jni methods.
const int kStatusError = -1;
const int kStatusOk = 0;
const int kStatusDecodeOnly = 2;
const int kStatusTryAgain = 3;

const int kHsDavidLogLevelDebug = 0;
const int kHsDavidLogLevelInformation = 1;
const int kHsDavidLogLevelWarning = 2;
const int kHsDavidLogLevelError = 3;

// Status codes specific to the JNI wrapper code.
enum JniStatusCode {
  kJniStatusOk = 0,
  kJniStatusOutOfMemory = -1,
  kJniStatusBufferAlreadyReleased = -2,
  kJniStatusInvalidNumOfPlanes = -3,
  kJniStatusBitDepth12NotSupportedWithYuv = -4,
  kJniStatusHighBitDepthNotSupportedWithSurfaceYuv = -5,
  kJniStatusANativeWindowError = -6,
  kJniStatusBufferResizeError = -7,
  kJniStatusNeonNotSupported = -8,
  kJniStatusNullPtr = -9,
  kJniStatusBitDepth10NotSupportedWithYuv=-10,
  kJniStatusJniException=-11,
};




// Manages frame buffer and reference information.
class JniFrameBuffer {
 public:
  explicit JniFrameBuffer(int id) : id_(id), reference_count_(0) {}
  ~JniFrameBuffer() {
    for (int plane_index = kPlaneY; plane_index < kMaxPlanes; plane_index++) {
      delete[] raw_buffer_[plane_index];
    }
  }

  // Not copyable or movable.
  JniFrameBuffer(const JniFrameBuffer&) = delete;
  JniFrameBuffer(JniFrameBuffer&&) = delete;
  JniFrameBuffer& operator=(const JniFrameBuffer&) = delete;
  JniFrameBuffer& operator=(JniFrameBuffer&&) = delete;

  void SetFrameData(Dav1dPicture *picture) {
    stride_[kPlaneY] = picture->stride[kPlaneY];
    stride_[kPlaneU] = stride_[kPlaneV] = picture->stride[kPlaneU];

    plane_[kPlaneY] = (uint8_t*) picture->data[kPlaneY];
    plane_[kPlaneU] = (uint8_t*) picture->data[kPlaneU];
    plane_[kPlaneV] = (uint8_t*) picture->data[kPlaneV];

    width_[kPlaneY] = picture->p.w;
    height_[kPlaneY] = picture->p.h;

    switch(picture->p.layout) {
      default:
      case DAV1D_PIXEL_LAYOUT_I420 : {
        width_[kPlaneU] = picture->p.w / 2;
        width_[kPlaneV] = picture->p.w / 2;

        height_[kPlaneU] = picture->p.h / 2;
        height_[kPlaneV] = picture->p.h / 2;
      } break;
      case DAV1D_PIXEL_LAYOUT_I422 : {
        width_[kPlaneU] = picture->p.w / 2;
        width_[kPlaneV] = picture->p.w / 2;

        height_[kPlaneU] = picture->p.h;
        height_[kPlaneV] = picture->p.h;
      } break;
      case DAV1D_PIXEL_LAYOUT_I444 : {
        width_[kPlaneU] = picture->p.w;
        width_[kPlaneV] = picture->p.w;

        height_[kPlaneU] = picture->p.h;
        height_[kPlaneV] = picture->p.h;
      } break;
      case DAV1D_PIXEL_LAYOUT_I400 : {
        width_[kPlaneU] = 0;
        width_[kPlaneV] = 0;

        height_[kPlaneU] = 0;
        height_[kPlaneV] = 0;
      } break;
    }

    isFrameDataSet_ = true;
  }

  int Stride(int plane_index) const { return stride_[plane_index]; }
  uint8_t* Plane(int plane_index) const { return plane_[plane_index]; }
  int DisplayedWidth(int plane_index) const {
    return width_[plane_index];
  }
  int DisplayedHeight(int plane_index) const {
    return height_[plane_index];
  }
  bool IsFrameDataSet() const { return isFrameDataSet_; }

  // Methods maintaining reference count are not thread-safe. They must be
  // called with a lock held.
  void AddReference() { ++reference_count_; }
  void RemoveReference() { reference_count_--; }
  bool InUse() const { return reference_count_ != 0; }
  int GetReferenceCount() const { return reference_count_; }

  uint8_t* RawBuffer(int plane_index) const { return raw_buffer_[plane_index]; }
  void* BufferPrivateData() const { return const_cast<int*>(&id_); }

  // Attempts to reallocate data planes if the existing ones don't have enough
  // capacity. Returns true if the allocation was successful or wasn't needed,
  // false if the allocation failed.
  bool MaybeReallocateDav1dDataPlanes(size_t y_plane_min_size,
                                      size_t uv_plane_min_size) {
    for (int plane_index = kPlaneY; plane_index < kMaxPlanes; plane_index++) {
      const size_t min_size =
          (plane_index == 0) ? y_plane_min_size : uv_plane_min_size;
      if (raw_buffer_size_[plane_index] >= min_size) continue;
      delete[] raw_buffer_[plane_index];
      raw_buffer_[plane_index] = new (std::nothrow) uint8_t[min_size];
      if (!raw_buffer_[plane_index]) {
        raw_buffer_size_[plane_index] = 0;
        return false;
      }
      raw_buffer_size_[plane_index] = min_size;
    }
    return true;
  }

 private:
  int stride_[kMaxPlanes];
  uint8_t* plane_[kMaxPlanes];
  int width_[kMaxPlanes];
  int height_[kMaxPlanes];
  const int id_;
  int reference_count_;
  // Pointers to the raw buffers allocated for the data planes.
  uint8_t* raw_buffer_[kMaxPlanes] = {};
  // Sizes of the raw buffers in bytes.
  size_t raw_buffer_size_[kMaxPlanes] = {};
  bool isFrameDataSet_ = false;
};

// Manages frame buffers used by libgav1 decoder and ExoPlayer.
// Handles synchronization between libgav1 and ExoPlayer threads.
class JniBufferManager {
 public:
  ~JniBufferManager() {
    // This lock does not do anything since libgav1 has released all the frame
    // buffers. It exists to merely be consistent with all other usage of
    // |all_buffers_| and |all_buffer_count_|.
    std::lock_guard<std::mutex> lock(mutex_);
    while (all_buffer_count_--) {
      delete all_buffers_[all_buffer_count_];
    }
  }

  JniStatusCode GetBuffer(size_t y_plane_min_size, size_t uv_plane_min_size,
                          JniFrameBuffer** jni_buffer) {
    std::lock_guard<std::mutex> lock(mutex_);

    JniFrameBuffer* output_buffer;
    if (free_buffer_count_) {
      output_buffer = free_buffers_[--free_buffer_count_];
    } else if (all_buffer_count_ < kMaxFrames) {
      output_buffer = new (std::nothrow) JniFrameBuffer(all_buffer_count_);
      if (output_buffer == nullptr) return kJniStatusOutOfMemory;
      all_buffers_[all_buffer_count_++] = output_buffer;
    } else {
      // Maximum number of buffers is being used.
      return kJniStatusOutOfMemory;
    }
    if (!output_buffer->MaybeReallocateDav1dDataPlanes(y_plane_min_size,
                                                       uv_plane_min_size)) {
      return kJniStatusOutOfMemory;
    }

    output_buffer->AddReference();
    *jni_buffer = output_buffer;

    return kJniStatusOk;
  }

  JniFrameBuffer* GetBuffer(int id) const { return all_buffers_[id]; }

  void AddBufferReference(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    all_buffers_[id]->AddReference();
  }

  JniStatusCode ReleaseBuffer(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    JniFrameBuffer* buffer = all_buffers_[id];
    if (!buffer->InUse()) {
      return kJniStatusBufferAlreadyReleased;
    }
    buffer->RemoveReference();
    if (!buffer->InUse()) {
      free_buffers_[free_buffer_count_++] = buffer;
    }
    return kJniStatusOk;
  }

  int GetBufferReferenceCount(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    JniFrameBuffer* buffer = all_buffers_[id];
    return buffer->GetReferenceCount();
  }

  int GetFreeBufferCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return free_buffer_count_;
  }

  int GetAllBufferCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_buffer_count_;
  }

 private:
  static const int kMaxFrames = 32;

  JniFrameBuffer* all_buffers_[kMaxFrames];
  int all_buffer_count_ = 0;

  JniFrameBuffer* free_buffers_[kMaxFrames];
  int free_buffer_count_ = 0;

  std::mutex mutex_;
};



struct JniContext {
  ~JniContext() {
    if (native_window) {
      ANativeWindow_release(native_window);
    }
  }

  bool MaybeAcquireNativeWindow(JNIEnv* env, jobject new_surface, bool isForceAquireSurface) {
    if ((surface == new_surface) && !isForceAquireSurface) {
      return true;
    }
    if (native_window) {
      ANativeWindow_release(native_window);
    }
    native_window_width = 0;
    native_window_height = 0;
    native_window = ANativeWindow_fromSurface(env, new_surface);
    if (native_window == nullptr) {
      jni_status_code = kJniStatusANativeWindowError;
      surface = nullptr;
      return false;
    }
    surface = new_surface;
    return true;
  }

  jfieldID decoder_private_field;
  jfieldID output_mode_field;
  jfieldID data_field;
  jmethodID init_for_private_frame_method;
  jmethodID init_for_yuv_frame_method;

  JniBufferManager buffer_manager;


  Dav1dContext *dav1d_context = nullptr;
  Dav1dSettings dav1d_settings;
  int dav1d_status_code = 0;

  ANativeWindow* native_window = nullptr;
  jobject surface = nullptr;
  int native_window_width = 0;
  int native_window_height = 0;

  JniStatusCode jni_status_code = kJniStatusOk;

  JNIEnv *env = nullptr;
  jobject callbackObj = nullptr;
//  jclass callbackClass = nullptr;
//  jmethodID getCallbackMID = nullptr;

  bool waiting_for_sequence_header = true;

  Dav1dData data;
};

class ScopedJNIEnv {
 public:
  ScopedJNIEnv() : attached(false), env(nullptr) {
    if (gHsDav1dJvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
      if (gHsDav1dJvm->AttachCurrentThread(&env, NULL) == 0) {
        attached = true;
      }
    }
  }

  ~ScopedJNIEnv() {
    if (attached) {
      gHsDav1dJvm->DetachCurrentThread();
    }
  }

  JNIEnv* getEnv() {
    return env;
  }

 private:
  bool attached;
  JNIEnv* env;
};


int dav1d_alloc_picture_callback(Dav1dPicture *picture, void *cookie) {
  JniContext *context = static_cast<JniContext*> (cookie);
  JniFrameBuffer* jni_buffer = nullptr;

  const int hbd = picture->p.bpc > 8;
  const int aligned_w = (picture->p.w + 127) & ~127;
  const int aligned_h = (picture->p.h + 127) & ~127;
  const int has_chroma = picture->p.layout != DAV1D_PIXEL_LAYOUT_I400;
  const int ss_ver = picture->p.layout == DAV1D_PIXEL_LAYOUT_I420;
  const int ss_hor = picture->p.layout != DAV1D_PIXEL_LAYOUT_I444;
  ptrdiff_t y_stride = aligned_w << hbd;
  ptrdiff_t uv_stride = has_chroma ? y_stride >> ss_hor : 0;
  /* Due to how mapping of addresses to sets works in most L1 and L2 cache
    * implementations, strides of multiples of certain power-of-two numbers
    * may cause multiple rows of the same superblock to map to the same set,
    * causing evictions of previous rows resulting in a reduction in cache
    * hit rate. Avoid that by slightly padding the stride when necessary. */
  if (!(y_stride & 1023))
    y_stride += DAV1D_PICTURE_ALIGNMENT;
  if (!(uv_stride & 1023) && has_chroma)
    uv_stride += DAV1D_PICTURE_ALIGNMENT;

  const size_t y_sz = y_stride * aligned_h;
  const size_t uv_sz = uv_stride * (aligned_h >> ss_ver);

  context->jni_status_code = context->buffer_manager.GetBuffer(y_sz, uv_sz,
                                                               &jni_buffer);
  if (context->jni_status_code != kJniStatusOk) {
    LOG_ERROR(context, "dav1d_alloc_picture_callback failed picture %p",
              picture);
    return DAV1D_ERR(ENOMEM);
  }

  picture->stride[0] = y_stride;
  picture->stride[1] = uv_stride;

  picture->data[kPlaneY] = jni_buffer->RawBuffer(kPlaneY);
  picture->data[kPlaneU] = jni_buffer->RawBuffer(kPlaneU);
  picture->data[kPlaneV] = jni_buffer->RawBuffer(kPlaneV);

  picture->allocator_data = jni_buffer->BufferPrivateData();

  LOG_DEBUG(context, "dav1d_alloc_picture_callback is called, all_buffer id %d used reference count %d picture %p",
            *(static_cast<const int*>(picture->allocator_data)), jni_buffer->GetReferenceCount(), picture);

  return 0;
}

void dav1d_release_picture_callback(Dav1dPicture *picture, void *cookie) {
  JniContext *context = static_cast<JniContext*> (cookie);
  const int buffer_id = *(static_cast<const int*>(picture->allocator_data));

  LOG_DEBUG(context, "dav1d_release_picture_callback is called, all_buffer id %d, free reference count %d picture %p",
            buffer_id, context->buffer_manager.GetBufferReferenceCount(buffer_id), picture);

  if (buffer_id < 0) {
    LOG_WARN(context, "dav1d_release_picture_callback is called with invalid buffer id %d", buffer_id);
    return;
  }

  context->jni_status_code = context->buffer_manager.ReleaseBuffer(buffer_id);
  if (context->jni_status_code != kJniStatusOk) {
    //TODO
    LOG_WARN(context, "dav1d_release_picture_callback ReleaseBuffer failed for buffer id %d, error %d",
             buffer_id, context->jni_status_code);
  }
}

constexpr int AlignTo16(int value) { return (value + 15) & (~15); }

void CopyPlane(const uint8_t* source, int source_stride, uint8_t* destination,
               int destination_stride, int width, int height) {
  while (height--) {
    std::memcpy(destination, source, width);
    source += source_stride;
    destination += destination_stride;
  }
}


void CopyFrameToDataBuffer(const Dav1dPicture* picture,
                           jbyte* data) {
  int length;
  jbyte* destination = data;

  // Luma
  length = picture->stride[kPlaneY] * picture->p.h;
  memcpy(destination, picture->data[kPlaneY], length);
  destination += length;

  // Chroma
  switch(picture->p.layout) {
    case DAV1D_PIXEL_LAYOUT_I420 : {
      length = (picture->stride[kPlaneU] * picture->p.h)/4;
    } break;
    case DAV1D_PIXEL_LAYOUT_I422 : {
      length = (picture->stride[kPlaneU] * picture->p.h)/2;
    } break;
    case DAV1D_PIXEL_LAYOUT_I444 : {
      length = (picture->stride[kPlaneU] * picture->p.h);
    } break;
    default:
    case DAV1D_PIXEL_LAYOUT_I400 : {
      length = 0;
    } break;
  }

  //U
  memcpy(destination, picture->data[kPlaneU], length);
  destination += length;

  //V
  memcpy(destination, picture->data[kPlaneV], length);
  destination += length;
}


int update_output_buffer(JniContext* context, jobject jOutputBuffer, Dav1dPicture *picture) {
  ScopedJNIEnv scopedEnv;
  JNIEnv* env = scopedEnv.getEnv();

  const int output_mode =
      env->GetIntField(jOutputBuffer, context->output_mode_field);
  if (output_mode == kOutputModeYuv) {
    // Resize the buffer if required. Default color conversion will be used as
    // libgav1::DecoderBuffer doesn't expose color space info.
    const jboolean init_result = env->CallBooleanMethod(
        jOutputBuffer, context->init_for_yuv_frame_method,
        picture->p.w /* width */,
        picture->p.h /* width */,
        picture->stride[kPlaneY] /* luma */, picture->stride[kPlaneU] /* chroma */,
        kColorSpaceUnknown);
    if (env->ExceptionCheck()) {
      // Exception is thrown in Java when returning from the native call.
      return kStatusError;
    }
    if (!init_result) {
      context->jni_status_code = kJniStatusBufferResizeError;
      return kStatusError;
    }

    const jobject data_object =
        env->GetObjectField(jOutputBuffer, context->data_field);
    jbyte* const data =
        reinterpret_cast<jbyte*>(env->GetDirectBufferAddress(data_object));

    switch (picture->p.bpc /* BitsPerpixelComponent */) {
      case 8:
        CopyFrameToDataBuffer(picture, data);
        break;
      case 10:
        context->jni_status_code = kJniStatusBitDepth10NotSupportedWithYuv; //TODO
        return kStatusError;
        break;
      default:
        context->jni_status_code = kJniStatusBitDepth12NotSupportedWithYuv;
        return kStatusError;
    }
  } else if (output_mode == kOutputModeSurfaceYuv) {
    if (picture->p.bpc != 8) {
      LOG_ERROR(context, "Higher bitdepth (%d) is not supported with surface view currently",
                picture->p.bpc);
      context->jni_status_code =
          kJniStatusHighBitDepthNotSupportedWithSurfaceYuv;
      return kStatusError;
    }

    const int buffer_id =
        *(static_cast<const int*>(picture->allocator_data));
    if (buffer_id < 0) {
      LOG_WARN(context, "update_output_buffer with invalid buffer id %d", buffer_id);
      return kStatusOk;
    } else {
      context->buffer_manager.AddBufferReference(buffer_id);
      LOG_DEBUG(context, "update_output_buffer with all_buffer id %d w %d h %d, used reference count %d",
                buffer_id, picture->p.w, picture->p.h, context->buffer_manager.GetBufferReferenceCount(buffer_id));
      JniFrameBuffer* const jni_buffer =
          context->buffer_manager.GetBuffer(buffer_id);
      jni_buffer->SetFrameData(picture);
      env->CallVoidMethod(jOutputBuffer, context->init_for_private_frame_method,
                          picture->p.w,
                          picture->p.h);
      if (env->ExceptionCheck()) {
        context->jni_status_code = kJniStatusJniException;
        // Exception is thrown in Java when returning from the native call.
        return kStatusError;
      }
      env->SetIntField(jOutputBuffer, context->decoder_private_field, buffer_id);
    }
  }

  return kStatusOk;
}

} //namespace

void set_jvm_pointer(JavaVM *jvm) {
  gHsDav1dJvm = jvm;
}

void log_callback(void *c, int logLevel, const char* message) {
  JniContext* context = static_cast<JniContext*> (c);
  ScopedJNIEnv scopedEnv;
  JNIEnv* env = scopedEnv.getEnv();
  if (env == nullptr || context->callbackObj == nullptr) {
    return;
  }

  jclass callbackClass = env->GetObjectClass(context->callbackObj);
  if (callbackClass != nullptr) {
    jmethodID getCallbackMID = env->GetStaticMethodID(
        callbackClass, "callback", "(ILjava/lang/String;)V");

    if (getCallbackMID != nullptr) {
      jstring jmessage = env->NewStringUTF(message);

      env->MonitorEnter(callbackClass); // Enter monitor
      env->CallStaticVoidMethod(callbackClass,
                                getCallbackMID,
                                logLevel,
                                jmessage);
      env->MonitorExit(callbackClass); // Enter monitor

      env->DeleteLocalRef(jmessage);
    }
    env->DeleteLocalRef(callbackClass);
  }
}

void dav1d_decoder_log_callback(void *cookie, const char *format, va_list ap) {
  JniContext* context = static_cast<JniContext*> (cookie);
  char str[128];
  vsnprintf(str, 127, format, ap);
  LOG_WARN(context, "DAV1D: %s", str);
}


int hsdav1d_send_input(void* c, const unsigned char* buffer, int length) {
  JniContext* context = static_cast<JniContext*> (c);

  LOG_DEBUG(context, "Sending input, length %d, buffer %p", length, buffer);

  if (context->waiting_for_sequence_header) {
    Dav1dSequenceHeader out;
    memset(&out, 0x00, sizeof(Dav1dSequenceHeader));
    int result = dav1d_parse_sequence_header(&out, buffer, length);
    if (result == 0) {
      LOG_DEBUG(context, "Found sequence header");
      context->waiting_for_sequence_header = false;
    } else {
      LOG_WARN(context, "Did not find sequence header, returning");
      return kStatusDecodeOnly;
    }
  }

  memset(&(context->data), 0x00, sizeof(Dav1dData));
  context->data.data = buffer;
  context->data.sz = length;

  context->dav1d_status_code = dav1d_send_data(context->dav1d_context, &(context->data));
  if (context->dav1d_status_code != 0) {
    if (context->dav1d_status_code == DAV1D_ERR(EAGAIN)) {
      LOG_WARN(context, "hsdav1d_send_input status AGAIN, data.sz %zu", context->data.sz);
      dav1d_data_unref(&(context->data));
      return kStatusTryAgain;
    }

    // TODO - May be we need to handle this case.
    LOG_ERROR(context, "hsdav1d_send_input context->dav1d_status_code %d", context->dav1d_status_code);
    dav1d_data_unref(&(context->data));
    return kStatusError;
  }

  if (context->data.sz > 0) {
    LOG_WARN(context, "After sending input, data.sz %zu", context->data.sz);
    // TODO - May be we need to handle this case.
    dav1d_data_unref(&(context->data));
  }

  return kStatusOk;
}



int hsdav1d_decode_process(void* c, jobject jOutputBuffer, bool isDecodeOnly) {
  JniContext* context = static_cast<JniContext*> (c);

  Dav1dPicture *picture;
  int result = kStatusOk;

  LOG_DEBUG(context, "hsdav1d_decode_process start 111, before dav1d_get_picture, waiting for sequence header %d",
            context->waiting_for_sequence_header);
  if (context->waiting_for_sequence_header) {
    return kStatusDecodeOnly;
  }

  picture = (Dav1dPicture *) calloc(1, sizeof(Dav1dPicture));
  context->dav1d_status_code = dav1d_get_picture(context->dav1d_context, picture);

  LOG_DEBUG(context, "dav1d_get_picture done, context->dav1d_status_code %d",
            context->dav1d_status_code);

  if (context->dav1d_status_code != 0) {
    if (context->dav1d_status_code == DAV1D_ERR(EAGAIN)) {
      LOG_WARN(context, "hsdav1d_decode_process status AGAIN");
      dav1d_picture_unref(picture);
      free(picture);
      return kStatusTryAgain;
    }

    LOG_DEBUG(context, "hsdav1d_decode_process status %d", context->dav1d_status_code);
    dav1d_picture_unref(picture);
    free(picture);
    return kStatusError;
  }

  if (isDecodeOnly || picture->data[kPlaneY] == nullptr) {
    LOG_DEBUG(context,  "Decode only, isNullPtr %d", (picture->data[kPlaneY] == nullptr));

    dav1d_picture_unref(picture);
    free(picture);
    return kStatusDecodeOnly;
  }

  result = update_output_buffer(context, jOutputBuffer, picture);

  // Reference count is already incremented for JNI buffer while updating output buffer. So calling unref.
  dav1d_picture_unref(picture);
  free(picture);

  return result;
}

void hsdav1d_flush_decoder(void* c) {
  JniContext* context = static_cast<JniContext*> (c);

  Dav1dPicture picture;
  int status = 0;
  int count = 0;
  LOG_DEBUG(context, "Flushing decoder, count free %d all %d",
            context->buffer_manager.GetFreeBufferCount(), context->buffer_manager.GetAllBufferCount());
  dav1d_flush(context->dav1d_context);
  context->waiting_for_sequence_header = true;

  LOG_DEBUG(context, "Getting pictures after flushing decoder, count free %d all %d",
            context->buffer_manager.GetFreeBufferCount(), context->buffer_manager.GetAllBufferCount());
  do {
    memset(&picture, 0x00, sizeof(Dav1dPicture));
    status = dav1d_get_picture(context->dav1d_context, &picture);
    dav1d_picture_unref(&picture);

    if (status == 0) {
      count++;
    }
  } while (status == 0);
  LOG_DEBUG(context, "Got %d pictures after flushing decoder", count);

  if (context->buffer_manager.GetFreeBufferCount() != context->buffer_manager.GetAllBufferCount()) {
    for (int i=0; i<context->buffer_manager.GetAllBufferCount(); i++) {
      LOG_DEBUG(context, "buffer id %d, reference count %d",
                i, context->buffer_manager.GetBufferReferenceCount(i));

      while (context->buffer_manager.GetBufferReferenceCount(i) > 0) {
        LOG_DEBUG(context, "buffer id %d, Releasing buffer reference count %d",
                  i, context->buffer_manager.GetBufferReferenceCount(i));
        context->buffer_manager.ReleaseBuffer(i);
      }
    }
  }
}


int hsdav1d_render_output_frame(void* c, jobject jSurface, jobject jOutputBuffer) {
  JniContext* context = static_cast<JniContext*> (c);

  JNIEnv *env = context->env;

  const int buffer_id =
      env->GetIntField(jOutputBuffer, context->decoder_private_field);

  if (buffer_id < 0) {
    LOG_WARN(context, "hsdav1d_render_output_frame with invalid buffer id %d", buffer_id);
    return kStatusOk;
  }

  JniFrameBuffer* const jni_buffer =
      context->buffer_manager.GetBuffer(buffer_id);
  if (!jni_buffer->IsFrameDataSet()) {
    LOG_WARN(context, "hsdav1d_render_output_frame frame data is not set for buffer id %d", buffer_id);
    return kStatusOk;
  }

  bool isForceAquireSurface = false;
  int acquireNativeWindowRetryCount = 2;
  ANativeWindow_Buffer native_window_buffer;

  do {
    if (!context->MaybeAcquireNativeWindow(env, jSurface, isForceAquireSurface)) {
      LOG_ERROR(context, "hsdav1d_render_output_frame MaybeAcquireNativeWindow failed, isForceAquireSurface %d", isForceAquireSurface);
      return kStatusError;
    }

    if (context->native_window_width != jni_buffer->DisplayedWidth(kPlaneY) ||
        context->native_window_height != jni_buffer->DisplayedHeight(kPlaneY)) {
      if (ANativeWindow_setBuffersGeometry(
          context->native_window, jni_buffer->DisplayedWidth(kPlaneY),
          jni_buffer->DisplayedHeight(kPlaneY), kImageFormatYV12)) {
        context->jni_status_code = kJniStatusANativeWindowError;
        LOG_ERROR(context, "ANativeWindow_setBuffersGeometry failed, buffer id %d w %d h %d, isForceAquireSurface %d",
                  buffer_id, jni_buffer->DisplayedWidth(kPlaneY),
                  jni_buffer->DisplayedHeight(kPlaneY), isForceAquireSurface);
        return kStatusError;
      }
      context->native_window_width = jni_buffer->DisplayedWidth(kPlaneY);
      context->native_window_height = jni_buffer->DisplayedHeight(kPlaneY);
    }

    if (ANativeWindow_lock(context->native_window, &native_window_buffer,
        /*inOutDirtyBounds=*/nullptr) ||
        native_window_buffer.bits == nullptr) {

      if (isForceAquireSurface) {
        LOG_ERROR(context, "ANativeWindow_lock failed, buffer id %d w %d h %d, isForceAquireSurface %d",
                  buffer_id, jni_buffer->DisplayedWidth(kPlaneY),
                  jni_buffer->DisplayedHeight(kPlaneY), isForceAquireSurface);
        context->jni_status_code = kJniStatusANativeWindowError;
        return kStatusError;
      } else {
        LOG_WARN(context, "ANativeWindow_lock failed, buffer id %d w %d h %d, isForceAquireSurface %d",
                 buffer_id, jni_buffer->DisplayedWidth(kPlaneY),
                 jni_buffer->DisplayedHeight(kPlaneY), isForceAquireSurface);
        isForceAquireSurface = true;
      }
    } else {
      break;
    }

    acquireNativeWindowRetryCount--;
  } while (acquireNativeWindowRetryCount > 0);

  // Y plane
  CopyPlane(jni_buffer->Plane(kPlaneY), jni_buffer->Stride(kPlaneY),
            reinterpret_cast<uint8_t*>(native_window_buffer.bits),
            native_window_buffer.stride, jni_buffer->DisplayedWidth(kPlaneY),
            jni_buffer->DisplayedHeight(kPlaneY));

  LOG_DEBUG(context, "hsdav1d_render_output_frame Y stride %d w %d h %d",
            jni_buffer->Stride(kPlaneY), jni_buffer->DisplayedWidth(kPlaneY),
            jni_buffer->DisplayedHeight(kPlaneY));

  const int y_plane_size =
      native_window_buffer.stride * native_window_buffer.height;
  const int32_t native_window_buffer_uv_height =
      (native_window_buffer.height + 1) / 2;
  const int native_window_buffer_uv_stride =
      AlignTo16(native_window_buffer.stride / 2);

  // TODO(b/140606738): Handle monochrome videos.

  // V plane
  // Since the format for ANativeWindow is YV12, V plane is being processed
  // before U plane.
  const int v_plane_height = std::min(native_window_buffer_uv_height,
                                      jni_buffer->DisplayedHeight(kPlaneV));
  CopyPlane(
      jni_buffer->Plane(kPlaneV), jni_buffer->Stride(kPlaneV),
      reinterpret_cast<uint8_t*>(native_window_buffer.bits) + y_plane_size,
      native_window_buffer_uv_stride, jni_buffer->DisplayedWidth(kPlaneV),
      v_plane_height);

  LOG_DEBUG(context, "hsdav1d_render_output_frame V stride %d w %d h %d min %d",
            jni_buffer->Stride(kPlaneV), jni_buffer->DisplayedWidth(kPlaneV),
            jni_buffer->DisplayedHeight(kPlaneV), v_plane_height);

  const int v_plane_size = v_plane_height * native_window_buffer_uv_stride;

  // U plane
  const int u_plane_height = std::min(native_window_buffer_uv_height,
                                      jni_buffer->DisplayedHeight(kPlaneU));
  CopyPlane(jni_buffer->Plane(kPlaneU), jni_buffer->Stride(kPlaneU),
            reinterpret_cast<uint8_t*>(native_window_buffer.bits) +
                y_plane_size + v_plane_size,
            native_window_buffer_uv_stride, jni_buffer->DisplayedWidth(kPlaneU),
            u_plane_height);

  LOG_DEBUG(context, "hsdav1d_render_output_frame U stride %d w %d h %d min %d",
            jni_buffer->Stride(kPlaneU), jni_buffer->DisplayedWidth(kPlaneU),
            jni_buffer->DisplayedHeight(kPlaneU), u_plane_height);

  if (ANativeWindow_unlockAndPost(context->native_window)) {
    context->jni_status_code = kJniStatusANativeWindowError;
    return kStatusError;
  }

  return kStatusOk;
}


void hsdav1d_release_output_frame(void* c, jobject jOutputBuffer) {
  JniContext* context = static_cast<JniContext*> (c);

  //JNIEnv *env = context->env;
  ScopedJNIEnv scopedEnv;
  JNIEnv* env = scopedEnv.getEnv();
  const int buffer_id =
      env->GetIntField(jOutputBuffer, context->decoder_private_field);

  if (buffer_id < 0) {
    LOG_WARN(context, "dav1d_release_output_frame with invalid buffer id %d", buffer_id);
    return;
  }

  LOG_DEBUG(context, "dav1d_release_output_frame all_buffer id %d, free reference count %d new new!!",
            buffer_id, context->buffer_manager.GetBufferReferenceCount(buffer_id));

  env->SetIntField(jOutputBuffer, context->decoder_private_field, -1);
  context->jni_status_code = context->buffer_manager.ReleaseBuffer(buffer_id);
  if (context->jni_status_code != kJniStatusOk) {
    //TODO
  }
}

void* hsdav1d_initialize_jni(int nThreads, jobject callback) {
  JNIEnv *env;
  JniContext* context = new (std::nothrow) JniContext();
  if (gHsDav1dJvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    // Handle error if necessary
    return nullptr;
  }

  context->env = env;
  context->callbackObj = env->NewGlobalRef(callback);

  // Populate JNI References.
  const jclass outputBufferClass = env->FindClass(
      "com/google/android/exoplayer2/decoder/VideoDecoderOutputBuffer");
  if (outputBufferClass != nullptr) {
    context->decoder_private_field =
        env->GetFieldID(outputBufferClass, "decoderPrivate", "I");
    context->output_mode_field = env->GetFieldID(outputBufferClass, "mode", "I");
    context->data_field =
        env->GetFieldID(outputBufferClass, "data", "Ljava/nio/ByteBuffer;");
    context->init_for_private_frame_method =
        env->GetMethodID(outputBufferClass, "initForPrivateFrame", "(II)V");
    context->init_for_yuv_frame_method =
        env->GetMethodID(outputBufferClass, "initForYuvFrame", "(IIIII)Z");

    // Clean up the local reference to outputBufferClass
    env->DeleteLocalRef(outputBufferClass);
  }

  //Print version
  LOG_INFO(context, "Dav1d version %s", dav1d_version());

  // Create decoder
  dav1d_default_settings(&(context->dav1d_settings));
  context->dav1d_settings.allocator.cookie = static_cast<void*> (context);
  context->dav1d_settings.allocator.alloc_picture_callback = dav1d_alloc_picture_callback;
  context->dav1d_settings.allocator.release_picture_callback = dav1d_release_picture_callback;

  //context->dav1d_settings.logger.callback = dav1d_decoder_log_callback;
  //context->dav1d_settings.logger.cookie = static_cast<void*> (context);
  //context->dav1d_settings.apply_grain = false;

  context->dav1d_settings.n_threads = nThreads;

  context->dav1d_status_code = dav1d_open(&(context->dav1d_context), &(context->dav1d_settings));

  return ((void*) context);
}

void hsdav1d_cleanup_jni(void* c) {
  JniContext* context = static_cast<JniContext*> (c);
  JNIEnv *env = context->env;
  if (context->callbackObj != nullptr) {
    env->DeleteGlobalRef(context->callbackObj);
  }
  delete context;
}