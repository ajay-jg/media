package androidx.media3.decoder;

import android.annotation.SuppressLint;
import androidx.annotation.CallSuper;
import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.util.Assertions;
import androidx.media3.common.util.Log;
import java.util.ArrayDeque;
import java.util.PriorityQueue;

/**
 * Base class for {@link Decoder}s that use their own decode thread and decode each input buffer
 * immediately into a corresponding output buffer.
 */
@SuppressWarnings("UngroupedOverloads")
public abstract class SimpleMultiThreadDecoder<
    I extends DecoderInputBuffer, O extends DecoderOutputBuffer, E extends DecoderException>
    implements Decoder<I, O, E> {

  private static final String TAG = "SimpleMultiThreadDecoder";

  private final Thread decodeThread;

  private final Object lock;
  private final ArrayDeque<I> queuedInputBuffers;
  private final ArrayDeque<O> queuedOutputBuffers;
  private final I[] availableInputBuffers;
  private final O[] availableOutputBuffers;

  private int availableInputBufferCount;
  private int availableOutputBufferCount;
  @Nullable private I dequeuedInputBuffer;

  @Nullable private E exception;
  private boolean flushed;
  private boolean released;
  private int skippedOutputBufferCount;

  private int numInputBufferWithDecoder = 0;
  private int numOutputBufferWithDecoder = 0;

  private PriorityQueue<Long> timestampQueue = new PriorityQueue<>();

  private boolean isEosOnInput = false;

  /**
   * @param inputBuffers An array of nulls that will be used to store references to input buffers.
   * @param outputBuffers An array of nulls that will be used to store references to output buffers.
   */
  @SuppressWarnings("nullness:method.invocation")
  protected SimpleMultiThreadDecoder(I[] inputBuffers, O[] outputBuffers) {
    lock = new Object();
    queuedInputBuffers = new ArrayDeque<>();
    queuedOutputBuffers = new ArrayDeque<>();
    availableInputBuffers = inputBuffers;
    availableInputBufferCount = inputBuffers.length;
    for (int i = 0; i < availableInputBufferCount; i++) {
      availableInputBuffers[i] = createInputBuffer();
    }
    availableOutputBuffers = outputBuffers;
    availableOutputBufferCount = outputBuffers.length;
    for (int i = 0; i < availableOutputBufferCount; i++) {
      availableOutputBuffers[i] = createOutputBuffer();
    }
    isEosOnInput = false;
    decodeThread =
        new Thread("ExoPlayer:SimpleMultiThreadDecoder") {
          @Override
          public void run() {
            SimpleMultiThreadDecoder.this.run();
          }
        };
    decodeThread.start();
  }

  /**
   * Sets the initial size of each input buffer.
   *
   * <p>This method should only be called before the decoder is used (i.e. before the first call to
   * {@link #dequeueInputBuffer()}.
   *
   * @param size The required input buffer size.
   */
  protected final void setInitialInputBufferSize(int size) {
    Assertions.checkState(availableInputBufferCount == availableInputBuffers.length);
    for (I inputBuffer : availableInputBuffers) {
      inputBuffer.ensureSpaceForWrite(size);
    }
  }

  @Override
  @Nullable
  public final I dequeueInputBuffer() throws E {
    synchronized (lock) {
      maybeThrowException();
      Assertions.checkState(dequeuedInputBuffer == null);
      dequeuedInputBuffer =
          availableInputBufferCount == 0
              ? null
              : availableInputBuffers[--availableInputBufferCount];
      return dequeuedInputBuffer;
    }
  }

  @Override
  public final void queueInputBuffer(I inputBuffer) throws E {
    synchronized (lock) {
      maybeThrowException();
      Assertions.checkArgument(inputBuffer == dequeuedInputBuffer);
      queuedInputBuffers.addLast(inputBuffer);
      maybeNotifyDecodeLoop();
      dequeuedInputBuffer = null;
    }
  }

  @Override
  @Nullable
  public final O dequeueOutputBuffer() throws E {
    synchronized (lock) {
      maybeThrowException();
      if (queuedOutputBuffers.isEmpty()) {
        return null;
      }
      return queuedOutputBuffers.removeFirst();
    }
  }

  /**
   * Releases an output buffer back to the decoder.
   *
   * @param outputBuffer The output buffer being released.
   */
  @CallSuper
  protected void releaseOutputBuffer(O outputBuffer) {
    synchronized (lock) {
      releaseOutputBufferInternal(outputBuffer);
      maybeNotifyDecodeLoop();
    }
  }

  @Override
  public final void flush() {
    synchronized (lock) {
      isEosOnInput = false;
      flushed = true;
      skippedOutputBufferCount = 0;
      if (dequeuedInputBuffer != null) {
        releaseInputBufferInternal(dequeuedInputBuffer);
        dequeuedInputBuffer = null;
      }
      while (!queuedInputBuffers.isEmpty()) {
        releaseInputBufferInternal(queuedInputBuffers.removeFirst());
      }
      while (!queuedOutputBuffers.isEmpty()) {
        queuedOutputBuffers.removeFirst().release();
      }
      timestampQueue.clear();
    }
  }

  @CallSuper
  @Override
  public void release() {
    synchronized (lock) {
      released = true;
      lock.notify();
    }
    try {
      decodeThread.join();
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
  }

  /**
   * Throws a decode exception, if there is one.
   *
   * @throws E The decode exception.
   */
  private void maybeThrowException() throws E {
    @Nullable E exception = this.exception;
    if (exception != null) {
      throw exception;
    }
  }

  /**
   * Notifies the decode loop if there exists a queued input buffer and an available output buffer
   * to decode into.
   *
   * <p>Should only be called whilst synchronized on the lock object.
   */
  private void maybeNotifyDecodeLoop() {
    if (canDecodeBuffer()) {
      lock.notify();
    }
  }

  private void run() {
    try {
      while (decode()) {
        // Do nothing.
      }
    } catch (InterruptedException e) {
      // Not expected.
      throw new IllegalStateException(e);
    }
  }

  @SuppressLint("Range")
  private boolean decode() throws InterruptedException {
    @Nullable E exception;

    boolean resetDecoder = false;

    synchronized (lock) {
      while (!released && !canDecodeBuffer()) {
        lock.wait();
      }

      if (released) {
        return false;
      }

      resetDecoder = flushed;
      flushed = false;
    }

    try {
      exception = decode(resetDecoder);
    } catch (RuntimeException e) {
      // This can occur if a sample is malformed in a way that the decoder is not robust against.
      // We don't want the process to die in this case, but we do want to propagate the error.
      exception = createUnexpectedDecodeException(e);
    } catch (OutOfMemoryError e) {
      // This can occur if a sample is malformed in a way that causes the decoder to think it
      // needs to allocate a large amount of memory. We don't want the process to die in this
      // case, but we do want to propagate the error.
      exception = createUnexpectedDecodeException(e);
    }
    if (exception != null) {
      synchronized (lock) {
        this.exception = exception;
      }
      return false;
    }

    synchronized (lock) {
      if (isEosOnInput) {
        Log.d(TAG, "Output buffer: in EOS state in decoder() Found EOS, output size "
            + availableOutputBufferCount);
      }

      if (isEosOnInput && availableOutputBufferCount > 0) {
        O outputBuffer = availableOutputBuffers[--availableOutputBufferCount];
        outputBuffer.addFlag(C.BUFFER_FLAG_END_OF_STREAM);

        outputBuffer.skippedOutputBufferCount = skippedOutputBufferCount;
        skippedOutputBufferCount = 0;
        queuedOutputBuffers.addLast(outputBuffer);
      }
    }

    return true;
  }

  @Nullable
  protected O getFreeOutputBuffer() {
    synchronized (lock) {
      if (released || availableOutputBufferCount <= 0) {
        return null;
      }

      numOutputBufferWithDecoder++;
      return availableOutputBuffers[--availableOutputBufferCount];
    }
  }

  @SuppressLint("Range")
  @Nullable
  protected I getFilledInputBuffer() {
    synchronized (lock) {
      if (released || queuedInputBuffers.isEmpty()) {
        return null;
      }
      numInputBufferWithDecoder++;
      I  inputBuffer = queuedInputBuffers.removeFirst();

      if (inputBuffer.isEndOfStream()) {
        Log.d(TAG, "Input buffer: Timestamp from queue "+inputBuffer.timeUs
            + " Found EOS, input size "+queuedInputBuffers.size());
        isEosOnInput = true;
      }
      return inputBuffer;
    }
  }

  protected void onOutputBufferFilled(O outputBuffer) {
    synchronized (lock) {
      numOutputBufferWithDecoder--;

      if (isEosOnInput) {
        outputBuffer.addFlag(C.BUFFER_FLAG_END_OF_STREAM);
      }

      if (flushed && !outputBuffer.isEndOfStream()) {
        outputBuffer.release();
      } else if (!outputBuffer.shouldBeSkipped && !outputBuffer.isEndOfStream()) {
        skippedOutputBufferCount++;
        outputBuffer.release();
      } else {
        if (!timestampQueue.isEmpty()) {
          outputBuffer.timeUs = timestampQueue.remove();
        }
//        Log.d(TAG, "Output buffer: Timestamp from queue "+outputBuffer.timeUs
//            + " decode only "+outputBuffer.isDecodeOnly()+" eos "+outputBuffer.isEndOfStream()
//            + " queue size "+timestampQueue.size());

        outputBuffer.skippedOutputBufferCount = skippedOutputBufferCount;
        skippedOutputBufferCount = 0;
        queuedOutputBuffers.addLast(outputBuffer);
      }
    }
  }

  protected void onInputBufferSendAgain(I inputBuffer) {
    synchronized (lock) {
//      Log.d(TAG, "Input buffer: onInputBufferSendAgain Timestamp "
//          + inputBuffer.timeUs+" flushed " + flushed);
      numInputBufferWithDecoder--;
      if (flushed) {
        releaseInputBufferInternal(inputBuffer);
      } else {
        queuedInputBuffers.addFirst(inputBuffer);
      }
    }
  }

  // Sometimes onInputBufferRelease maybe called before onInputBufferProcessing.
  // Hence the calling method should save the timestamp of the input buffer before feeding to
  // the decoder and pass the same here.
  protected void onInputBufferProcessing(I inputBuffer, long inputBufferTimeStampUs, boolean canRelease) {
    synchronized (lock) {
      timestampQueue.add(inputBufferTimeStampUs);
//      Log.d(TAG, "Input buffer: onInputBufferProcessing Adding to Timestamp from queue "
//          +inputBufferTimeStampUs+" canRelease "+canRelease+" queue size "+timestampQueue.size()+""
//          + " is decoder only "+inputBuffer.isDecodeOnly());
      if (canRelease) {
        numInputBufferWithDecoder--;
        releaseInputBufferInternal(inputBuffer);
      }
    }
  }

  protected void onInputBufferRelease(I inputBuffer) {
    synchronized (lock) {
//      Log.d(TAG, "Input buffer: onInputBufferRelease Timestamp "
//          +inputBuffer.timeUs);
      numInputBufferWithDecoder--;
      releaseInputBufferInternal(inputBuffer);
    }
  }

  private boolean canDecodeBuffer() {
    return !queuedInputBuffers.isEmpty() && availableOutputBufferCount > 0;
  }

  private void releaseInputBufferInternal(I inputBuffer) {
    inputBuffer.clear();
    availableInputBuffers[availableInputBufferCount++] = inputBuffer;
  }

  private void releaseOutputBufferInternal(O outputBuffer) {
    outputBuffer.clear();
    availableOutputBuffers[availableOutputBufferCount++] = outputBuffer;
  }

  /** Creates a new input buffer. */
  protected abstract I createInputBuffer();

  /** Creates a new output buffer. */
  protected abstract O createOutputBuffer();

  /**
   * Creates an exception to propagate for an unexpected decode error.
   *
   * @param error The unexpected decode error.
   * @return The exception to propagate.
   */
  protected abstract E createUnexpectedDecodeException(Throwable error);

  /**
   * Decodes the {@code inputBuffer} and stores any decoded output in {@code outputBuffer}.
   *
   * @param reset Whether the decoder must be reset before decoding.
   * @return A decoder exception if an error occurred, or null if decoding was successful.
   */
  @Nullable
  protected abstract E decode(boolean reset);
}
