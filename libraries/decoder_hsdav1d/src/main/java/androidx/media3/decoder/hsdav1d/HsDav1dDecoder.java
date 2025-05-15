package androidx.media3.decoder.hsdav1d;

import static androidx.annotation.VisibleForTesting.PACKAGE_PRIVATE;

import android.util.Log;
import android.view.Surface;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.media3.common.C;
import androidx.media3.common.util.Util;
import androidx.media3.decoder.DecoderInputBuffer;
import androidx.media3.decoder.SimpleDecoder;
import androidx.media3.decoder.VideoDecoderOutputBuffer;
import java.nio.ByteBuffer;

@VisibleForTesting(otherwise = PACKAGE_PRIVATE)
public class HsDav1dDecoder extends
    SimpleDecoder<DecoderInputBuffer, VideoDecoderOutputBuffer, HsDav1dDecoderException> {

  private static final String TAG = "HsDav1dDecoder";
  private static final String DECODER_NAME = "com.google.android.exoplayer2.ext.hsdav1d";

  private static final int DAV1D_ERROR = -1;
  private static final int DAV1D_OK = 0;
  private static final int DAV1D_DECODE_ONLY = 2;

  private static final int DAV1D_DECODE_TRY_AGAIN = 3;

  private long dav1dDecoderJniContext = 0;

  static HsDav1dJniLogger hsDav1dJniLogger = new HsDav1dJniLogger();

  @C.VideoOutputMode private volatile int outputMode;

  private boolean need_more_input = true;

  private VideoDecoderOutputBuffer outputBufferFromLastCall = null;
  private final Object outputBufferLock = new Object();

  public HsDav1dDecoder(
      int numInputBuffers, int numOutputBuffers, int initialInputBufferSize, int threads)
      throws HsDav1dDecoderException {
    super(new DecoderInputBuffer[numInputBuffers], new VideoDecoderOutputBuffer[numOutputBuffers]);
    if (!HsDav1dLibrary.isAvailable()) {
      throw new HsDav1dDecoderException("Failed to load dav1d decoder native library.");
    }

    Log.d(TAG, "Initializing Dav1d JNI, threads "+threads);
    dav1dDecoderJniContext = HsDavidSwigGenerated.hsdav1d_initialize_jni(threads, hsDav1dJniLogger);
    if (dav1dDecoderJniContext == 0) {
      Log.e(TAG, "Failed to initialize Dav1d JNI");
      throw new HsDav1dDecoderException("Failed to initialize Dav1d JNI");
    } else {
      Log.d(TAG, "Successfully initialized Dav1d JNI, ptr "+ dav1dDecoderJniContext);
    }

    setInitialInputBufferSize(initialInputBufferSize);
  }


  @Override
  public String getName() {
    return DECODER_NAME;
  }

  @Override
  protected DecoderInputBuffer createInputBuffer() {
    return new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DIRECT);
  }

  @Override
  protected VideoDecoderOutputBuffer createOutputBuffer() {
    return new VideoDecoderOutputBuffer(this::releaseOutputBuffer);
  }

  @Nullable
  @Override
  protected HsDav1dDecoderException decode(DecoderInputBuffer inputBuffer,
      VideoDecoderOutputBuffer outputBuffer, boolean reset) {

    int result = 0;

    ByteBuffer inputData = Util.castNonNull(inputBuffer.data);
    int inputSize = inputData.limit();

    boolean have_input = true;
    boolean have_output = true;

    boolean decodeOnly = inputBuffer.isDecodeOnly();
    if (!decodeOnly) {
      outputBuffer.init(inputBuffer.timeUs, outputMode, /* supplementalData= */ null);
      outputBuffer.format = inputBuffer.format;
      outputBuffer.decoderPrivate = -1;
    }

//    Log.d(TAG, "Dav1d decoder sending input: size "+inputSize+" reset "+reset+""
//        + " time "+inputBuffer.timeUs+" input flags "+inputBuffer.isEndOfStream()+" "
//        + " "+inputBuffer.isKeyFrame());

    if (reset) {
      Log.d(TAG, "Dav1d decoder resetting");
      HsDavidSwigGenerated.hsdav1d_flush_decoder(dav1dDecoderJniContext);
      synchronized (outputBufferLock) {
        if (outputBufferFromLastCall != null) {
          if (outputBuffer != outputBufferFromLastCall) {
            outputBufferFromLastCall.addFlag(C.BUFFER_FLAG_DECODE_ONLY);
          }
          outputBufferFromLastCall = null;
        }
      }
      need_more_input = true;
    }

    int count = 0;
    int max_count = 10;

    while ((have_input || have_output) && (count < max_count)) {
      count++;
//      Log.d(TAG, "Dav1d decoder loop count: "+count+" have_input "+have_input+" "
//          + "have_output "+have_output+" need_more_input "+need_more_input);
      if (!need_more_input && !have_output) {
        break;
      }

      // Need an input and we have it.
      if (need_more_input && have_input) {
        result = HsDavidSwigGenerated
            .hsdav1d_send_input(dav1dDecoderJniContext, inputData, inputSize);
        if (result == DAV1D_OK) {
          have_input = false;
        } else if (result == DAV1D_ERROR) {
          Log.e(TAG,
              "Dav1d decoder error during dav1d_send_input: size " + inputSize + " result "
                  + result);
          return new HsDav1dDecoderException(
              "Dav1d decoder error during dav1d_decode_input: result " + result);
        } else if (result == DAV1D_DECODE_TRY_AGAIN) {
          need_more_input = false;
        } else if (result == DAV1D_DECODE_ONLY) {
//          Log.d(TAG,
//              "Dav1d decoder dav1d_send_input Received decode only error");
          have_input = false;
        }
      }

      // We have an output buffer
      if (have_output) {
        boolean fed_output = false;
        synchronized (outputBufferLock) {

          if (outputBufferFromLastCall != null && outputBuffer == outputBufferFromLastCall) {
//            Log.d(TAG, "Dav1d decoder, last output buffer is same as current output "
//                + "buffer!!, making it null");
            outputBufferFromLastCall = null;
          }

          if (outputBufferFromLastCall != null) {
//            Log.d(TAG, "Dav1d decoder First processing last output, isDecodeOnly "
//                + outputBufferFromLastCall.isDecodeOnly());

            result = HsDavidSwigGenerated.hsdav1d_decode_process(dav1dDecoderJniContext,
                outputBufferFromLastCall,
                outputBufferFromLastCall.isDecodeOnly());
//            Log.d(TAG, "Dav1d decoder dav1d_decode_process result with last buffer "
//                + result+" timestamp "+outputBufferFromLastCall.timeUs+" buffer id "
//                + outputBufferFromLastCall.decoderPrivate);
            if (result == DAV1D_OK) {
              outputBufferFromLastCall = null;
            } else if (result == DAV1D_ERROR) {
              Log.e(TAG,
                  "Dav1d decoder error during dav1d_decode_process on last buffer: result "
                      + result);
              return new HsDav1dDecoderException(
                  "Dav1d decoder error during dav1d_decode_process on last buffer: result " + result);
            } else if (result == DAV1D_DECODE_TRY_AGAIN) {
              Log.e(TAG, "Dav1d decoder process try again for last buffer");
            } else if (result == DAV1D_DECODE_ONLY) {
              outputBufferFromLastCall.addFlag(C.BUFFER_FLAG_DECODE_ONLY);
              outputBufferFromLastCall = null;
            }
            need_more_input = true;
            fed_output = true;
          }
        }

        if (!fed_output) {
//          Log.d(TAG, "Dav1d decoder processing new output");
          result = HsDavidSwigGenerated.hsdav1d_decode_process(dav1dDecoderJniContext,
              outputBuffer,
              decodeOnly);
//          Log.d(TAG, "Dav1d decoder dav1d_decode_process result with new buffer "
//              + result+" timestamp "+outputBuffer.timeUs+" buffer id "
//              +outputBuffer.decoderPrivate);
          if (result == DAV1D_OK) {
            have_output = false;
          } else if (result == DAV1D_ERROR) {
            Log.e(TAG,
                "Dav1d decoder error during dav1d_decode_process on new buffer: result "
                    + result);
            return new HsDav1dDecoderException(
                "Dav1d decoder error during dav1d_decode_process on new buffer: result "
                    + result);
          } else if (result == DAV1D_DECODE_TRY_AGAIN) {
            Log.e(TAG, "Dav1d decoder process try again for new buffer");
            if (!have_input) {
              break;
            }
          } else if (result == DAV1D_DECODE_ONLY) {
            outputBuffer.addFlag(C.BUFFER_FLAG_DECODE_ONLY);
            have_output = false;
          }
          need_more_input = true;
        }
      }
    }

    if (have_output) {
      synchronized (outputBufferLock) {
        outputBufferFromLastCall = outputBuffer;
      }
    }

    return null;
  }

  @Override
  protected HsDav1dDecoderException createUnexpectedDecodeException(Throwable error) {
    return new HsDav1dDecoderException("Unexpected decode error", error);
  }

  @Override
  public void release() {
    super.release();
//    Log.d(TAG, "Releasing Dav1d decoder");
    HsDavidSwigGenerated.hsdav1d_cleanup_jni(dav1dDecoderJniContext);
  }

  @Override
  protected void releaseOutputBuffer(VideoDecoderOutputBuffer buffer) {
    synchronized (outputBufferLock) {
      if (outputBufferFromLastCall == buffer) {
//        Log.d(TAG, "Releasing frame is outputBufferFromLastCall, all_buffer id "+
//            buffer.decoderPrivate+" is decode only "+outputBufferFromLastCall.isDecodeOnly());
        outputBufferFromLastCall.addFlag(C.BUFFER_FLAG_DECODE_ONLY);
      }
    }

    // Decode only frames do not acquire a reference on the internal decoder buffer and thus do not
    // require a call to HsDavidJniWrap.release_output_frame.
    if (buffer.mode == C.VIDEO_OUTPUT_MODE_SURFACE_YUV && !buffer.isDecodeOnly()) {
//      Log.d(TAG, "Releasing frame, timestamp "+buffer.timeUs);
      HsDavidSwigGenerated.hsdav1d_release_output_frame(dav1dDecoderJniContext, buffer);
    } else {
      if (buffer.decoderPrivate >= 0) {
//        Log.d(TAG, "Not Releasing frame for all_buffer id "+buffer.decoderPrivate+" "
//            + " decodeOnly "+buffer.isDecodeOnly()+" mode "+buffer.mode);
      }
    }

    super.releaseOutputBuffer(buffer);
  }

  public void setOutputMode(@C.VideoOutputMode int outputMode) {
    this.outputMode = outputMode;
  }

  public boolean renderToSurface(VideoDecoderOutputBuffer outputBuffer, Surface surface)
      throws HsDav1dDecoderException {
    if (outputBuffer.mode != C.VIDEO_OUTPUT_MODE_SURFACE_YUV) {
      throw new HsDav1dDecoderException("Invalid output mode.");
    }

    synchronized (outputBufferLock) {
      if (outputBufferFromLastCall == outputBuffer) {
        outputBufferFromLastCall.addFlag(C.BUFFER_FLAG_DECODE_ONLY);
//        Log.d(TAG, "Output buffer time " + outputBuffer.timeUs
//            + ", processing pending, adding decode only flag, buffer id "+outputBuffer.decoderPrivate);
      } else {
//        Log.d(TAG, "Output buffer time " + outputBuffer.timeUs+", buffer id "+
//            outputBuffer.decoderPrivate);
      }
    }

//    Log.d(TAG, "Rendering frame, timestamp "+outputBuffer.timeUs+" isSurfaceValid "
//        +surface.isValid());

    int status = HsDavidSwigGenerated
        .hsdav1d_render_output_frame(dav1dDecoderJniContext, surface, outputBuffer);
    if (status == DAV1D_ERROR) {
      Log.e(TAG, "Buffer render error: " + status);
      throw new HsDav1dDecoderException(
          "Buffer render error: " + status);
    }

    return true;
  }
}
