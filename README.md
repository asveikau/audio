# libaudio

This project attempts cross-platform audio playback in various formats.

It is subject to the following design considerations:

* Decode and play audio in a file format and OS agnostic way.
* Abstract file I/O from decoding.  I/O always happens through a [pure virtual class][1] and doesn't necessarily need a file.
* Use platform-specific decoders where available, but for others, statically link to permissively-licensed open source decoders.  (No GPL.)

## API

Browsing in [`include`][2] or the [tests directory][3] is probably your best start.
Overview of functionality:

* [`AudioDevice.h`][4] abstracts the output device, giving various platforms a synchronous write() style interface.

* [`AudioSource.h`][5] abstracts decoding and file format.
    - [`AudioCodec.h`][6] is how an application instantiates these.

* [`AudioPlayer.h`][7] ties the two together.
    - `audio::Player` is a synchronous API that allows you to "step through" packet at a time.
    - `audio::ThreadedPlayer` can be used with a high-priority worker thread to expose a Play/Stop itnerface.

## Requirements

Building happens via [the makefiles submodule][8].

    $ git submodule update --init
    $ make                             # or "gmake" on some platforms, like BSD

On Unix, the project builds with g++ 8.0 or higher (7 and earlier won't work!)
or clang++.

On Windows, GNU make, nasm and msysgit should be on PATH, and the project is
typically tested with VS2015 with Windows SDK 10586.

## Platform support

Currently there are audio playback drivers for:

* Windows: WASAPI (Vista+), WinMM (WinXP)
* Mac: CoreAudio
* Linux: ALSA, OSS
* FreeBSD: OSS
* OpenBSD: sndio
* NetBSD and Solaris: audio(4)

Proprietary codec support can come from:

* MediaFoundation (Windows 7+)
* CoreAudio ExtAudioFile (Mac)

The following is from the third_party subdir and may be linked statically:

* Android OpenCore: MP3, AAC, AMR
* libalac
* libflac
* libopusfile 
* libvorbisfile

[1]: https://github.com/asveikau/common/blob/master/include/common/c%2B%2B/stream.h
[2]: https://github.com/asveikau/audio/tree/master/include
[3]: https://github.com/asveikau/audio/blob/master/src/test
[4]: https://github.com/asveikau/audio/blob/master/include/AudioDevice.h
[5]: https://github.com/asveikau/audio/blob/master/include/AudioSource.h
[6]: https://github.com/asveikau/audio/blob/master/include/AudioCodec.h
[7]: https://github.com/asveikau/audio/blob/master/include/AudioPlayer.h
[8]: https://github.com/asveikau/makefiles

