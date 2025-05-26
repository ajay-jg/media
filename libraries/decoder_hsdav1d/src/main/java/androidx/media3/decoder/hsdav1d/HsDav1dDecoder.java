package androidx.media3.decoder.hsdav1d;

import static androidx.annotation.VisibleForTesting.PACKAGE_PRIVATE;

import android.util.Log;
import android.view.Surface;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.util.Util;
import androidx.media3.decoder.DecoderInputBuffer;
import androidx.media3.decoder.SimpleMultiThreadDecoder;
import androidx.media3.decoder.VideoDecoderOutputBuffer;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

@VisibleForTesting(otherwise = PACKAGE_PRIVATE)
public class HsDav1dDecoder extends
    SimpleMultiThreadDecoder<DecoderInputBuffer, VideoDecoderOutputBuffer, HsDav1dDecoderException> {

  private static final String TAG = "HsDav1dDecoder";
  private static final String DECODER_NAME = "com.google.android.exoplayer2.ext.hsdav1d";

  private static final int DAV1D_ERROR = -1;
  private static final int DAV1D_OK = 0;
  private static final int DAV1D_DECODE_ONLY = 2;
  private static final int DAV1D_DECODE_TRY_AGAIN = 3;

  private long dav1dDecoderJniContext = 0;
  static HsDav1dJniLogger hsDav1dJniLogger = new HsDav1dJniLogger();
  private Format format = null;
  private boolean isInputEosReceived = false;
  @C.VideoOutputMode private volatile int outputMode;
  private boolean need_more_input = true;
  private int numberOfInputBeforeFirstOutput = 0;
  private boolean receivedFirstOutput = false;
  private int inputBufferCounter = 0;
  private final boolean isCopyInputBuffer;
  Object nativeSurfaceUpdateLock = new Object();
  private boolean needNativeSurfaceUpdate = true;

  private List<HsDav1dDecoderInputBuffer> inputBufferList = Collections.synchronizedList(new ArrayList<>());

  public HsDav1dDecoder(
      int numInputBuffers, int numOutputBuffers, int initialInputBufferSize,
      int threads, int frameDelay, boolean isCopyInputBuffer)
      throws HsDav1dDecoderException {
    super(new HsDav1dDecoderInputBuffer[numInputBuffers], new VideoDecoderOutputBuffer[numOutputBuffers]);
    if (!HsDav1dLibrary.isAvailable()) {
      throw new HsDav1dDecoderException("Failed to load dav1d decoder native library.");
    }

    this.isCopyInputBuffer = isCopyInputBuffer;
    this.needNativeSurfaceUpdate = false;

    Log.d(TAG, "Initializing Dav1d JNI, threads "+threads+" frame delay "+frameDelay+
        " copy input "+isCopyInputBuffer);
    dav1dDecoderJniContext = HsDavidSwigGenerated.hsdav1d_initialize_jni(threads,
        frameDelay,
        isCopyInputBuffer,
        hsDav1dJniLogger,
        this);
    if (dav1dDecoderJniContext == 0) {
      Log.e(TAG, "Failed to initialize Dav1d JNI");
      throw new HsDav1dDecoderException("Failed to initialize Dav1d JNI");
    } else {
      Log.d(TAG, "Successfully initialized Dav1d JNI, ptr "+ dav1dDecoderJniContext);
    }

    setInitialInputBufferSize(initialInputBufferSize);
  }

  // This method is called by JNI once the input buffer is consumed in isCopyInputBuffer = false
  // case.
  public void onInputBufferConsumed(int id) {
    synchronized (inputBufferList) {
//      Log.d(TAG, "In onInputBufferConsumed, id " + id
//          + " remaining size " + inputBufferList.size());
      for (HsDav1dDecoderInputBuffer buffer : inputBufferList) {
        if (buffer.getId() == id) {
          inputBufferList.remove(buffer);
          onInputBufferRelease(buffer);

//          Log.d(TAG, "In onInputBufferConsumed, Found id "
//              + id + " remaining size " + inputBufferList.size());
          break;
        }
      }
    }
  }

  @Override
  public String getName() {
    return DECODER_NAME;
  }

  @Override
  public void setOutputStartTimeUs(long outputStartTimeUs) {

  }

  @Override
  protected synchronized HsDav1dDecoderInputBuffer createInputBuffer() {
    return new HsDav1dDecoderInputBuffer(
        DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DIRECT, ++inputBufferCounter);
  }

  @Override
  protected VideoDecoderOutputBuffer createOutputBuffer() {
    return new VideoDecoderOutputBuffer(this::releaseOutputBuffer);
  }

  @Nullable
  private HsDav1dDecoderException drain_decoder(boolean isFlushingDecoder) {
    int count = 0;
    int result = 0;

    if (isInputEosReceived || isFlushingDecoder) {
      HsDavidSwigGenerated.hsdav1d_flush_decoder(dav1dDecoderJniContext);
    }

    boolean feed_more_output = true;
    while (feed_more_output) {
      count++;
//      Log.d(TAG, "Dav1d decoder Loop count " + count + " Proceeding to output");
      VideoDecoderOutputBuffer outputBuffer = getFreeOutputBuffer();
      if (outputBuffer == null) {
        Log.e(TAG, "Dav1d decoder could not get free output buffer");
        break;
      } else {
        outputBuffer.mode = outputMode;
        outputBuffer.format = format;
        result = HsDavidSwigGenerated.hsdav1d_decode_process(dav1dDecoderJniContext,
            outputBuffer);
//        Log.d(TAG, "Dav1d decoder processing output, result " + result);
        if (result == DAV1D_OK) {
          onOutputBufferFilled(outputBuffer);

          if (!isInputEosReceived && !isFlushingDecoder) {
            if (!receivedFirstOutput) {
              Log.i(TAG, "Dav1d produced first output after numInput "
                  + numberOfInputBeforeFirstOutput);
              receivedFirstOutput = true;
            }
            break;
          } else {
            Log.d(TAG, "Dav1d decoder processing output, in EOS or flush");
          }
        } else if (result == DAV1D_ERROR) {
          Log.e(TAG,
              "Dav1d decoder error during dav1d_decode_process, result "
                  + result);
          return new HsDav1dDecoderException(
              "Dav1d decoder error during dav1d_decode_process, result " + result);
        } else if (result == DAV1D_DECODE_TRY_AGAIN) {
          need_more_input = true;
          if (isInputEosReceived) {
            outputBuffer.addFlag(C.BUFFER_FLAG_END_OF_STREAM);
          } else {
            outputBuffer.shouldBeSkipped = true;
          }
          feed_more_output = false;
        } else if (result == DAV1D_DECODE_ONLY) {
          outputBuffer.shouldBeSkipped = true; // replace this flag with outputBuffer.shouldBeSkipped = true
        }

        onOutputBufferFilled(outputBuffer);
      }
    }
    return null;
  }

  @Nullable
  @Override
  protected HsDav1dDecoderException decode(boolean reset) {
    boolean used_input = false;
    int result = 0;

    if (reset) {
      Log.d(TAG, "Dav1d decoder resetting");
      isInputEosReceived = false;
      drain_decoder(true);
      need_more_input = true;
    }

    int count = 0;
    while (need_more_input) {
      count++;
      result = 0;
//      Log.d(TAG, "Dav1d decoder Loop count "+count+" need_more_input ");
      used_input = false;
      HsDav1dDecoderInputBuffer hsDav1dDecoderInputBuffer = null;
      int bufferId = -1;

      DecoderInputBuffer inputBuffer = getFilledInputBuffer();
      if (!isCopyInputBuffer && (inputBuffer instanceof HsDav1dDecoderInputBuffer)) {
        hsDav1dDecoderInputBuffer = (HsDav1dDecoderInputBuffer) inputBuffer;
        bufferId = hsDav1dDecoderInputBuffer.getId();
      }
      long inputBufferTimeStampUs = inputBuffer.timeUs;

      if (inputBuffer == null) {
        Log.w(TAG, "Dav1d decoder could not get filled input buffer");
        break;
      } else {
        ByteBuffer inputData = Util.castNonNull(inputBuffer.data);
        int inputSize = inputData.limit();

        format = inputBuffer.format;
        isInputEosReceived = inputBuffer.isEndOfStream();
        if (isInputEosReceived) {
          Log.d(TAG, "Dav1d decoder input buffer with EOS received, Size "+inputSize);
          used_input = true;
          need_more_input = false;
          HsDavidSwigGenerated.hsdav1d_received_eos(dav1dDecoderJniContext);
        } else {

          if (hsDav1dDecoderInputBuffer != null) {
            synchronized (inputBufferList) {
              inputBufferList.add(hsDav1dDecoderInputBuffer);
            }
          }

          result = HsDavidSwigGenerated
              .hsdav1d_send_input(dav1dDecoderJniContext, inputData, inputSize, bufferId);
//          Log.d(TAG, "Dav1d decoder Sent input, result " + result+" bufferId "+bufferId);
          if (result == DAV1D_OK) {
            if (!receivedFirstOutput) {
              numberOfInputBeforeFirstOutput++;
            }
            used_input = true;
            onInputBufferProcessing(inputBuffer, inputBufferTimeStampUs, isCopyInputBuffer);
            break;
          } else if (result == DAV1D_ERROR) {
            Log.e(TAG,
                "Dav1d decoder error during dav1d_send_input: size " + inputSize + " result "
                    + result);
            return new HsDav1dDecoderException(
                "Dav1d decoder error during dav1d_decode_input: result " + result);
          } else if (result == DAV1D_DECODE_TRY_AGAIN) {
            need_more_input = false;
            isInputEosReceived = false;
          } else if (result == DAV1D_DECODE_ONLY) {
            used_input = true;
          }
        }

        if (result != DAV1D_OK) {
          if (hsDav1dDecoderInputBuffer != null) {
            synchronized (inputBufferList) {
              inputBufferList.remove(hsDav1dDecoderInputBuffer);
            }
          }
        }
      }

      if (result == DAV1D_DECODE_ONLY || isInputEosReceived) {
        onInputBufferRelease(inputBuffer);
      } else if (used_input) {
        onInputBufferProcessing(inputBuffer, inputBufferTimeStampUs,
            isCopyInputBuffer);
      } else {
        onInputBufferSendAgain(inputBuffer);
      }
    }

    return drain_decoder(false);
  }

  @Override
  protected HsDav1dDecoderException createUnexpectedDecodeException(Throwable error) {
    return new HsDav1dDecoderException("Unexpected decode error", error);
  }

  @Override
  public void release() {
    super.release();
    Log.d(TAG, "Releasing Dav1d decoder");
    HsDavidSwigGenerated.hsdav1d_cleanup_jni(dav1dDecoderJniContext);
    Log.d(TAG, "Released Dav1d decoder, May clear");
  }

  @Override
  protected void releaseOutputBuffer(VideoDecoderOutputBuffer buffer) {

    // Decode only frames do not acquire a reference on the internal decoder buffer and thus do not
    // require a call to HsDavidSwigGenerated.release_output_frame.
    if (buffer.mode == C.VIDEO_OUTPUT_MODE_SURFACE_YUV && !buffer.shouldBeSkipped) {
//      Log.d(TAG, "Releasing frame, timestamp "+buffer.timeUs);
      HsDavidSwigGenerated.hsdav1d_release_output_frame(dav1dDecoderJniContext, buffer);
    }

    super.releaseOutputBuffer(buffer);
  }

  public void setOutputMode(@C.VideoOutputMode int outputMode) {
    Log.d(TAG, "setOutputMode is called, "+outputMode);
    this.outputMode = outputMode;

    synchronized (nativeSurfaceUpdateLock) {
      this.needNativeSurfaceUpdate = true;
    }
  }

  public boolean renderToSurface(VideoDecoderOutputBuffer outputBuffer, Surface surface)
      throws HsDav1dDecoderException {

    if (outputBuffer.mode != C.VIDEO_OUTPUT_MODE_SURFACE_YUV) {
      throw new HsDav1dDecoderException("Invalid output mode.");
    }

    if (isInputEosReceived) {
      Log.d(TAG, "Not rendering frame, timestamp "+outputBuffer.timeUs+" as EOS is received ");
      return false;
    }

//    Log.d(TAG, "Rendering frame, timestamp "+outputBuffer.timeUs+" isSurfaceValid "
//        +surface.isValid());

    boolean needSurfaceUpdate;
    synchronized (nativeSurfaceUpdateLock) {
      needSurfaceUpdate = needNativeSurfaceUpdate;
      needNativeSurfaceUpdate = false;
    }
    int status = HsDavidSwigGenerated
        .hsdav1d_render_output_frame(dav1dDecoderJniContext, surface,
            outputBuffer, needSurfaceUpdate);
    if (status == DAV1D_ERROR) {
      Log.e(TAG, "Buffer render error: " + status);
      throw new HsDav1dDecoderException(
          "Buffer render error: " + status);
    }

    return true;
  }
}