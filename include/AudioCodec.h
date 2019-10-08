/*
 Copyright (C) 2017, 2018, 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/
//
// "Codec" is a bit of a miseadling name.  This class is basically a factory
// for an audio::Source, meaning it handles file format detection and sometimes
// parsing, usually by dispatching to the correct subclass of audio::Source.
//

#ifndef audiocodec_h_
#define audiocodec_h_

#include "AudioSource.h"
#include "AudioTags.h"

namespace audio {

// Hints that the caller can provide, eg. from a container or stream
// implementation.
//
struct CodecArgs
{
   uint64_t Duration;
   MetadataReceiver *Metadata;

   CodecArgs() : Duration(0), Metadata(nullptr) {}
};

struct Codec : public RefCountable
{
   virtual int GetBytesRequiredForDetection() { return 0; }
   virtual void TryOpen(
      common::Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   ) = 0;
};

// Attempt to initialize all the "codec objects" in a global list.
//
void RegisterCodecs(void);

// Enumerate registered codec objects and call ->TryOpen to get a source.
//
void OpenCodec(
   common::Stream *file,
   CodecArgs *params,
   Source **obj,
   error *err
);

//
// Initialization for library-internal codec classes follows.
// Usually audio::RegiterCodecs is actually all that you need.
//

void RegisterCodec(Codec *codec, error *err);

void RegisterWavCodec(error *err);
void RegisterMfCodec(error *err);
void RegisterCoreAudioCodec(error *err);

void RegisterOpenCoreAmrCodec(error *err);

void CreateOpenCoreAacCodec(Codec **out, error *err);
void CreateOpenCoreMp3Codec(Codec **out, error *err);
void RegisterAdtsCodec(error *err);

void RegisterMp4Codec(error *err);
void RegisterMp4CodecForMetadataParse(error *err);

void CreateVorbisSource(
   common::Stream *file, 
   CodecArgs &params,
   Source **obj,
   error *err
);

void CreateOpusSource(
   common::Stream *file,
   CodecArgs &params,
   Source **obj,
   error *err
);

void CreateFlacSource(
   common::Stream *file,
   bool isOgg,
   CodecArgs &params,
   Source **obj,
   error *err
);

void RegisterOggCodec(error *err);

void RegisterFlacCodec(error *err);

} // namespace
#endif
