/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include <common/misc.h>
#include <common/logger.h>
#include <common/c++/registrationlist.h>

#include "codecs/id3.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

using namespace common;
using namespace audio;

static
RegistrationList<Codec>
codecList;

void audio::RegisterCodec(Codec *codec, error *err)
{
   codecList.Register(codec, err);
}

void audio::OpenCodec(Stream *file, CodecArgs *params, Source **obj, error *err)
{
   int n = 0;
   void *start = nullptr;
   Pointer<Source> newObject;
   bool id3Checked = false;
   uint64_t origin = 0;
   CodecArgs paramsStorage;

   if (!params)
   {
      params = &paramsStorage;
   }

   if (!codecList.HasItems())
      ERROR_SET(err, unknown, "No codecs registered.");

   n = sizeof(Id3Header);

   codecList.ForEach(
      [&n] (Codec *p, error *err) -> void
      {
         n = MAX(n, p->GetBytesRequiredForDetection());
      },
      err
   );
   ERROR_CHECK(err);

   n += 8192;

retry:
   if (n)
   {
      if (!start)
         start = malloc(n);
      if (!start)
         ERROR_SET(err, nomem);

      n = file->Read(start, n, err);
      ERROR_CHECK(err);

      file->Seek(origin, SEEK_SET, err);
      ERROR_CHECK(err);
   }

   if (!id3Checked && n > sizeof(Id3Header))
   {
      auto id3 = (const Id3Header*)start;
      if (id3->HasMagic())
      {
         auto newOrigin = id3->ReadSize() + 10;
         file->Seek(newOrigin, SEEK_SET, err);
         if (ERROR_FAILED(err))
            error_clear(err);
         else
         {
            log_printf("Skipping ID3 tag of %d bytes", newOrigin);
            id3Checked = true;
            origin = newOrigin;
            goto retry;
         }
      }
   }

   codecList.TryLoad(
      file,
      newObject.GetAddressOf(),
      [&] (Codec *codec, Source **newObject, error *err) -> void
      {
         if (n < codec->GetBytesRequiredForDetection())
            goto exit;

         codec->TryOpen(file, start, n, *params, newObject, err);
         ERROR_CHECK(err);
      exit:;
      },
      err
   );
   ERROR_CHECK(err);

   if (!newObject.Get())
      ERROR_SET(err, unknown, "Could not find codec object.");
exit:
   if (ERROR_FAILED(err)) newObject = nullptr;
   free(start);
   *obj = newObject.Detach();
}

void
audio::RegisterCodecs(void)
{
   error err;

   // NB:
   // Codec objects are attempted first-in, last-out.
   // Registrations that come later get attempted first.
   //

#if defined(USE_OPENCORE_AAC) || defined(USE_OPENCORE_MP3)
   RegisterAdtsCodec(&err);
   error_clear(&err);
#endif

#if defined(HAVE_MP4_DEMUX)
   RegisterMp4Codec(&err);
   error_clear(&err);
#endif

#if defined(__APPLE__)
   RegisterCoreAudioCodec(&err);
   error_clear(&err);
#endif

#if defined (_WINDOWS)
   RegisterMfCodec(&err);
   error_clear(&err);
#endif

#if defined(USE_OPENCORE_AMR)
   RegisterOpenCoreAmrCodec(&err);
   error_clear(&err);
#endif

   RegisterOggCodec(&err);
   error_clear(&err);

   RegisterFlacCodec(&err);
   error_clear(&err);

   RegisterWavCodec(&err);
   error_clear(&err);
}
