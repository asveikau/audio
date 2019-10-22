/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

//
// This file represents a source object that doesn't do any fancy container
// stuff, like ADTS or MP4.  Straight up decode without parsing frame
// headers.  i.e. for a codec within a mp4.
//

#ifndef micro_codec_h_

#include "AudioSource.h"

namespace audio {

struct MicroCodec : public common::RefCountable
{
   virtual void Initialize(
      const void *config,
      int nbytes,
      error *err
   ) = 0;
   virtual const char *Describe() { return nullptr; }
   virtual void GetMetadata(Metadata *md, error *err) = 0;
   virtual int Decode(
      const void *samples,
      int samplesNBytes,
      void *outputBuffer,
      int outputBufferNBytes,
      error *err
   ) = 0;
};

struct MicroCodecDemux
{
   uint32_t FrameSize, Duration;
};

void
AudioSourceFromMicroCodec(
   MicroCodec *codec,
   common::Stream *demuxer,
   uint64_t duration,
   const void *config,
   int nbytes,
   Source **out,
   error *err
);

void
CreateAlacCodec(
   MicroCodec **out,
   error *err
);

} // end namespace

#endif
