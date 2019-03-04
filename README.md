# libaudio

This project attempts cross-platform audio playback in various formats.

It is subject to the following design considerations:

* Decode and play audio in a file format and OS agnostic way.
* Abstract file I/O from decoding.  I/O always happens through a pure virtual class and doesn't necessarily need a file.
* Use platform-specific decoders where available, but for others, statically link to permissively-licensed open source decoders.  (No GPL.)

## API

Browsing in `include` or the test app is probably your best start.  Overview of
functionality:

* `AudioDevice.h` abstracts the output device, giving various platforms a synchronous write() style interface.

* `AudioSource.h` abstracts decoding and file format.
    - `AudioCodec.h` is how an application instantiates these.

* `AudioPlayer.h` ties the two together.
    - `audio::Player` is a synchronous API that allows you to "step through" packet at a time.
    - `audio::ThreadedPlayer` can be used with a high-priority worker thread to expose a Play/Stop itnerface.

## Requirements

Building happens via [the makefiles submodule][1].

    $ git submodule update --init
    $ make                             # or "gmake" on some platforms, like BSD

On Unix, the project builds with g++ 8.0 or higher (7 and earlier won't work!)
or clang++.

On Windows, GNU make, nasm and msysgit should be on PATH, and the project is
typically tested with VS2015 with Windows SDK 10586.

[1]: https://github.com/asveikau/makefiles

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
* libflac
* libopusfile 
* libvorbisfile

