/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include <common/misc.h>
#include <common/c++/new.h>

#include <errno.h>
#include <string.h>

using namespace common;
using namespace audio;

namespace {

struct OggDispatcher : public Codec
{
public:
   int GetBytesRequiredForDetection()
   {
      return 0x1c + 8;
   }

   void TryOpen(
      common::Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   )
   {
      const void *pastHeader = (const char*)firstBuffer + 0x1c;

      if (memcmp(firstBuffer, "OggS", 4))
         return;
      else if (!memcmp(pastHeader, "\1vorbis", 7))
         CreateVorbisSource(file, params, obj, err);
      else if (!memcmp(pastHeader, "OpusHead", 8))
         CreateOpusSource(file, params, obj, err);
      else if (!memcmp(pastHeader, "\x7f""FLAC", 5))
         CreateFlacSource(file, true, params, obj, err);
   }
};

} // end namespace

void audio::RegisterOggCodec(error *err)
{
   Pointer<OggDispatcher> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
   RegisterCodec(p.Get(), err);
   ERROR_CHECK(err);
exit:;
}

