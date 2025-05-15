package androidx.media3.decoder.hsdav1d;

import androidx.media3.common.util.Log;

public class HsDav1dJniLogger {
  private static final String TAG = "HsDav1dJniLogger";

  private static final int LOG_LEVEL_DEBUG = 0;
  private static final int LOG_LEVEL_INFORMATION = 1;
  private static final int LOG_LEVEL_WARNING= 2;
  private static final int LOG_LEVEL_ERROR = 3;


  public static void callback(int level, String message) {
    switch(level) {
      default:
      case LOG_LEVEL_DEBUG: {
        Log.d(TAG, "JNI: " + message);
      } break;

      case LOG_LEVEL_INFORMATION: {
        Log.i(TAG, "JNI: " + message);
      } break;

      case LOG_LEVEL_WARNING: {
        Log.w(TAG, "JNI: " + message);
      } break;

      case LOG_LEVEL_ERROR: {
        Log.e(TAG, "JNI: " + message);
      } break;
    }
  }
}