package androidx.media3.exoplayer.source.ads;

import androidx.media3.common.AdPlaybackState;
import androidx.media3.common.Timeline;

public interface MultiPeriodAdTimelineFactory {

  Timeline create(Timeline contentTimeline, AdPlaybackState adPlaybackState);
}