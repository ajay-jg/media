package androidx.media3.decoder.hsdav1d;

import androidx.media3.decoder.DecoderInputBuffer;

public class HsDav1dDecoderInputBuffer extends DecoderInputBuffer {

  private int id;

  public HsDav1dDecoderInputBuffer(int bufferReplacementMode, int id) {
    super(bufferReplacementMode);
    this.id = id;
  }

  public int getId() {
    return id;
  }
}