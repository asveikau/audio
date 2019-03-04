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

using namespace common;
using namespace audio;

namespace {

struct AdtsProbe : public Codec
{
   void TryOpen(
      common::Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   )
   {
      Pointer<Source> r;
      Pointer<Codec> codecs[2];
      enum { Mp3, Aac };
      const unsigned char *p = (const unsigned char *)firstBuffer;
      int off = 0;

      for (; firstBufferSize >= 4; ++off, ++p, --firstBufferSize)
      {
         if (p[0] == 0xff && (p[1] & 0xe0) == 0xe0)
         {
            bool mpg25 = (p[1] & 0xf0) == 0xe0;
            int layer = 4 - ((p[1] >> 1) & 3);
            Codec **codec = nullptr;

            if (mpg25 && layer != 3)
               continue;

            if (layer == 4)
            {
               codec = codecs[Aac].GetAddressOf();
#ifdef USE_OPENCORE_AAC
               if (!*codec)
               {
                  CreateOpenCoreAacCodec(codec, err);
                  ERROR_CHECK(err);
               }
#endif
            }
            else
            {
               codec = codecs[Mp3].GetAddressOf();
#ifdef USE_OPENCORE_MP3
               if (!*codec)
               {
                  CreateOpenCoreMp3Codec(codec, err);
                  ERROR_CHECK(err);
               }
#endif
            }

            if (*codec)
            {
               if (off)
               {
                  file->Seek(off, SEEK_CUR, err);
                  ERROR_CHECK(err);
                  off = 0;
               }
               (*codec)->TryOpen(
                  file,
                  p,
                  firstBufferSize,
                  params,
                  r.GetAddressOf(),
                  err  
               );
               if (ERROR_FAILED(err))
               {
                  error_clear(err);
                  r = nullptr;
               }
               if (r.Get())
                  goto exit;
            }
         }
      }

   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *obj = r.Detach();
   }
};

} // namespace

void audio::RegisterAdtsCodec(error *err)
{
   Pointer<AdtsProbe> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
   RegisterCodec(p.Get(), err);
   ERROR_CHECK(err);
exit:;
}
