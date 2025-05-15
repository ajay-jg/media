# ExoPlayer `hsdav1d` module

The `hsdav1d` module provides `Hsdav1dVideoRenderer` developed by Hotstar, which uses dav1d native
library to decode AV1 videos.

Note: We have referred [Libvgav1VideoRenderer][] for the implementation.

[Libvgav1VideoRenderer]: https://github.com/google/ExoPlayer/blob/release-v2/extensions/av1/README.md

## License note

Property of Hotstar.

## Dependency

It depends on Hotstar's [dav1d][].

[dav1d]: https://github.com/hotstar/dav1d

## Update JNI Interface (Optional)

* Install [SWIG][] if you want to modify JNI interface.

[SWIG]: https://www.swig.org/Doc1.3/Java.html

```
brew install swig
```
Update the [hsdav1d_swig_interface.i][] file and execute [hsdav1d_create_jni_interface_swig.sh][] to
update JNI interface.

[hsdav1d_swig_interface.i]: https://github.com/google/ExoPlayer/blob/Hotstar-Android-Exoplayer_2.16.1/extensions/hsdav1d/src/main/jni/hsdav1d_swig_jni_script/hsdav1d_swig_interface/hsdav1d_swig_interface.i
[hsdav1d_create_jni_interface_swig.sh]: https://github.com/google/ExoPlayer/blob/Hotstar-Android-Exoplayer_2.16.1/extensions/hsdav1d/src/main/jni/hsdav1d_swig_jni_script/hsdav1d_create_jni_interface_swig.sh

## Install CMake
* [Install CMake][].

Gradle will build the module automatically when run on the command line or via Android Studio,
using [CMake][] and [Ninja][] to configure and build dav1d and the module's [JNI wrapper library][].

[top level README]: https://github.com/google/ExoPlayer/blob/release-v2/README.md
[Install CMake]: https://developer.android.com/studio/projects/install-ndk
[CMake]: https://cmake.org/
[Ninja]: https://ninja-build.org
[JNI wrapper library]: https://github.com/google/ExoPlayer/blob/Hotstar-Android-Exoplayer_2.16.1/extensions/hsdav1d/src/main/jni/hsdav1d/CMakeLists.txt

## Using the module

Once you've followed the instructions above to check out, build and depend on
the module, the next step is to tell ExoPlayer to use `HsDav1dVideoRenderer`.
How you do this depends on which player API you're using:

*   If you're passing a `DefaultRenderersFactory` to `ExoPlayer.Builder`, you
    can enable using the module by setting the `extensionRendererMode` parameter
    of the `DefaultRenderersFactory` constructor to
    `EXTENSION_RENDERER_MODE_ON`. This will use `HsDav1dVideoRenderer` for
    playback if `MediaCodecVideoRenderer` doesn't support decoding the input AV1
    stream. Pass `EXTENSION_RENDERER_MODE_PREFER` to give `HsDav1dVideoRenderer`
    priority over `MediaCodecVideoRenderer`.
*   If you've subclassed `DefaultRenderersFactory`, add a
    `HsDav1dVideoRenderer` to the output list in `buildVideoRenderers`.
    ExoPlayer will use the first `Renderer` in the list that supports the input
    media format.
*   If you've implemented your own `RenderersFactory`, return a
    `HsDav1dVideoRenderer` instance from `createRenderers`. ExoPlayer will use
    the first `Renderer` in the returned array that supports the input media
    format.
*   If you're using `ExoPlayer.Builder`, pass a `HsDav1dVideoRenderer` in the
    array of `Renderer`s. ExoPlayer will use the first `Renderer` in the list
    that supports the input media format.

Note 1: These instructions assume you're using `DefaultTrackSelector`. If you have
a custom track selector the choice of `Renderer` is up to your implementation.
You need to make sure you are passing a `HsDav1dVideoRenderer` to the player and
then you need to implement your own logic to use the renderer for a given track.

Note 2: Currently `HsDav1dVideoRenderer` has higher priority over `Libvgav1VideoRenderer`. The
`DefaultRenderersFactory` needs modification to change this behaviour.

## Using the module in the demo application

To try out playback using the module in the [demo application][], see
[enabling extension decoders][].

[demo application]: https://exoplayer.dev/demo-application.html
[enabling extension decoders]: https://exoplayer.dev/demo-application.html#enabling-extension-decoders

## Rendering options

There are two possibilities for rendering the output `HsDav1dVideoRenderer`
gets from the dav1d decoder:

*   GL rendering using GL shader for color space conversion

    *   If you are using `ExoPlayer` with `PlayerView` or `StyledPlayerView`,
        enable this option by setting `surface_type` of view to be
        `video_decoder_gl_surface_view`.
    *   Otherwise, enable this option by sending `HsDav1dVideoRenderer` a
        message of type `Renderer.MSG_SET_VIDEO_OUTPUT`
        with an instance of `VideoDecoderOutputBufferRenderer` as its object.
        `VideoDecoderGLSurfaceView` is the concrete
        `VideoDecoderOutputBufferRenderer` implementation used by
        `(Styled)PlayerView`.

*   Native rendering using `ANativeWindow`

    *   If you are using `ExoPlayer` with `PlayerView` or `StyledPlayerView`,
        this option is enabled by default.
    *   Otherwise, enable this option by sending `HsDav1dVideoRenderer` a
        message of type `Renderer.MSG_SET_VIDEO_OUTPUT` with an instance of
        `SurfaceView` as its object.

Note from [Libvgav1VideoRenderer][] (We have not tested this for `HsDav1dVideoRenderer`):
Although the default option uses `ANativeWindow`, based on our testing the GL rendering mode
has better performance, so should be preferred

## Links

* [Javadoc][]

[Javadoc]: https://exoplayer.dev/doc/reference/index.html