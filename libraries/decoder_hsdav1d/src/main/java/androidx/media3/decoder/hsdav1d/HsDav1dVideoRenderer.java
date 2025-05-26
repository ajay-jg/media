package androidx.media3.decoder.hsdav1d;


import static androidx.media3.exoplayer.DecoderReuseEvaluation.REUSE_RESULT_YES_WITHOUT_RECONFIGURATION;

import android.os.Handler;
import android.view.Surface;
import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.util.Log;
import androidx.media3.common.util.TraceUtil;
import androidx.media3.common.util.Util;
import androidx.media3.decoder.CryptoConfig;
import androidx.media3.decoder.DecoderException;
import androidx.media3.decoder.VideoDecoderOutputBuffer;
import androidx.media3.exoplayer.DecoderReuseEvaluation;
import androidx.media3.exoplayer.ExoPlaybackException;
import androidx.media3.exoplayer.RendererCapabilities;
import androidx.media3.exoplayer.video.DecoderVideoRenderer;
import androidx.media3.exoplayer.video.VideoRendererEventListener;


public class HsDav1dVideoRenderer extends DecoderVideoRenderer {

  public static final int THREAD_COUNT_AUTODETECT = 0;
  public static final int FRAME_DELAY_AUTODETECT = 0;
  private static final String TAG = "HsDav1dVideoRenderer";

  private static final int DEFAULT_NUM_OF_INPUT_BUFFERS = 4;
  private static final int DEFAULT_NUM_OF_OUTPUT_BUFFERS = 4;
  private static final boolean IS_COPY_INPUT_BUFFER = false;
  /**
   * Default input buffer size in bytes, based on 720p resolution video compressed by a factor of
   * two.
   */
  private static final int DEFAULT_INPUT_BUFFER_SIZE =
      Util.ceilDivide(1920, 64) * Util.ceilDivide(1080, 64) * (64 * 64 * 3 / 2) / 2;

  /** The number of input buffers. */
  private final int numInputBuffers;
  /**
   * The number of output buffers. The renderer may limit the minimum possible value due to
   * requiring multiple output buffers to be dequeued at a time for it to make progress.
   */
  private final int numOutputBuffers;

  private final int threads;

  private final int frameDelay;

  private final boolean isCopyInputBuffer;

  @Nullable private HsDav1dDecoder decoder;

  private long renderedFrameCount = 0;

  private Surface outputSurface = null;

  @Override
  public String getName() {
    return TAG;
  }

  @Override
  public int supportsFormat(Format format) throws ExoPlaybackException {
    if (!MimeTypes.VIDEO_AV1.equalsIgnoreCase(format.sampleMimeType)
        || !HsDav1dLibrary.isAvailable()) {
      return RendererCapabilities.create(C.FORMAT_UNSUPPORTED_TYPE);
    }
    if (format.cryptoType != C.CRYPTO_TYPE_NONE) {
      return RendererCapabilities.create(C.FORMAT_UNSUPPORTED_DRM);
    }
    return RendererCapabilities.create(
        C.FORMAT_HANDLED, ADAPTIVE_SEAMLESS, TUNNELING_NOT_SUPPORTED);
  }

  @Override
  public void setPlaybackSpeed(float currentPlaybackSpeed, float targetPlaybackSpeed)
      throws ExoPlaybackException {
    super.setPlaybackSpeed(currentPlaybackSpeed, targetPlaybackSpeed);
  }

  @Override
  public long getRenderedFrameCount() {
    return renderedFrameCount;
  }

  @Override
  protected HsDav1dDecoder createDecoder(
      Format format, @Nullable CryptoConfig cryptoConfig) throws DecoderException {
    TraceUtil.beginSection("createDav1dDecoder");
    Log.d(TAG, "createDav1dDecoder, maxInputSize "+format.maxInputSize+""
        + " numInputBuffers "+numInputBuffers+" numOutputBuffers "+numOutputBuffers+""
        + " threads "+threads+" frameDelay "+frameDelay+" isCopyInputBuffer "+isCopyInputBuffer);
    int initialInputBufferSize =
        format.maxInputSize != Format.NO_VALUE ? format.maxInputSize : DEFAULT_INPUT_BUFFER_SIZE;
    HsDav1dDecoder decoder =
        new HsDav1dDecoder(numInputBuffers, numOutputBuffers, initialInputBufferSize,
            threads, frameDelay, isCopyInputBuffer);
    this.decoder = decoder;
    TraceUtil.endSection();
    return decoder;
  }

  @Override
  protected void renderOutputBufferToSurface(VideoDecoderOutputBuffer outputBuffer, Surface surface)
      throws HsDav1dDecoderException {
    if (decoder == null) {
      throw new HsDav1dDecoderException(
          "Failed to render output buffer to surface: Dav1d decoder is not initialized.");
    }

    if (decoder.renderToSurface(outputBuffer, surface)) {
      renderedFrameCount++;
    }
    outputBuffer.release();
  }

  @Override
  protected void setDecoderOutputMode(int outputMode) {
    if (decoder != null) {
      decoder.setOutputMode(outputMode);
    }
  }

  @Override
  protected DecoderReuseEvaluation canReuseDecoder(
      String decoderName, Format oldFormat, Format newFormat) {
    return new DecoderReuseEvaluation(
        decoderName,
        oldFormat,
        newFormat,
        REUSE_RESULT_YES_WITHOUT_RECONFIGURATION,
        /* discardReasons= */ 0);
  }

  public HsDav1dVideoRenderer(
      long allowedJoiningTimeMs,
      @Nullable Handler eventHandler,
      @Nullable VideoRendererEventListener eventListener,
      int maxDroppedFramesToNotify,
      int threadCount,
      int frameDelay,
      boolean isCopyInputBuffer) {
    this(
        allowedJoiningTimeMs,
        eventHandler,
        eventListener,
        maxDroppedFramesToNotify,
        threadCount,
        DEFAULT_NUM_OF_INPUT_BUFFERS,
        DEFAULT_NUM_OF_OUTPUT_BUFFERS,
        frameDelay,
        isCopyInputBuffer);
  }

  public HsDav1dVideoRenderer(
      long allowedJoiningTimeMs,
      @Nullable Handler eventHandler,
      @Nullable VideoRendererEventListener eventListener,
      int maxDroppedFramesToNotify) {
    this(
        allowedJoiningTimeMs,
        eventHandler,
        eventListener,
        maxDroppedFramesToNotify,
        THREAD_COUNT_AUTODETECT,
        DEFAULT_NUM_OF_INPUT_BUFFERS,
        DEFAULT_NUM_OF_OUTPUT_BUFFERS,
        FRAME_DELAY_AUTODETECT,
        IS_COPY_INPUT_BUFFER);
  }

  public HsDav1dVideoRenderer(
      long allowedJoiningTimeMs,
      @Nullable Handler eventHandler,
      @Nullable VideoRendererEventListener eventListener,
      int maxDroppedFramesToNotify,
      int threads,
      int numInputBuffers,
      int numOutputBuffers,
      int frameDelay,
      boolean isCopyInputBuffer) {
    super(allowedJoiningTimeMs, eventHandler, eventListener, maxDroppedFramesToNotify);
    Log.d(TAG, "entered HsDav1dVideoRenderer constructor");
    this.threads = threads;
    this.numInputBuffers = numInputBuffers;
    this.numOutputBuffers = numOutputBuffers;
    this.frameDelay = frameDelay;
    this.isCopyInputBuffer = isCopyInputBuffer;

    Log.d(TAG, "Exiting HsDav1dVideoRenderer constructor");
  }
}