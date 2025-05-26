#ifndef __HSDAV1D_WRAPPER__
#define __HSDAV1D_WRAPPER__

void* hsdav1d_initialize_jni(int nThreads,
                             int maxFrameDelay,
                             bool isCopyInputBuffer,
                             jobject logCallback,
                             jobject inputBufferReleaseCallback);
void hsdav1d_cleanup_jni(void* c);
int hsdav1d_send_input(void* c, const unsigned char* buffer, int length, int bufferId);
int hsdav1d_decode_process(void* c, jobject jOutputBuffer);
void hsdav1d_flush_decoder(void* c);
int hsdav1d_render_output_frame(void* c, jobject jSurface, jobject jOutputBuffer,
                                bool needsSurfaceUpdate);
void hsdav1d_release_output_frame(void* c, jobject jOutputBuffer);
void hsdav1d_received_eos(void* c);

#endif //__HSDAV1D_WRAPPER__