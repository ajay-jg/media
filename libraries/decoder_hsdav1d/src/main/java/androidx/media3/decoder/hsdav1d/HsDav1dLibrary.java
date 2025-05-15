package androidx.media3.decoder.hsdav1d;

import androidx.media3.common.MediaLibraryInfo;
import androidx.media3.common.util.LibraryLoader;

public class HsDav1dLibrary {
  static {
    MediaLibraryInfo.registerModule("hs.exo.dav1d");
  }

  private static final LibraryLoader LOADER = new LibraryLoader("hsdav1dJNI") {
    @Override
    protected void loadLibrary(String name) {
      System.loadLibrary(name);
    }
  };

  private HsDav1dLibrary() {}

  /** Returns whether the underlying library is available, loading it if necessary. */
  public static boolean isAvailable() {
    return LOADER.isAvailable();
  }
}
