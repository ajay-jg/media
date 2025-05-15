package androidx.media3.decoder.hsdav1d;

import androidx.media3.decoder.DecoderException;

public class HsDav1dDecoderException extends DecoderException {

  /* package */ HsDav1dDecoderException(String message) {
    super(message);
  }

  /* package */ HsDav1dDecoderException(String message, Throwable cause) {
    super(message, cause);
  }
}