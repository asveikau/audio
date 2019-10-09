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
#include <string>

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

void audio::OnOggComments(
   MetadataReceiver *recv,
   char **comments,
   int *lengths,
   int nComments,
   char *vendor,
   error *err
)
{
   for (int i=0; i<nComments; ++i)
   {
      enum TypeEnum
      {
         String,
         Integer,
      };
      struct Tag
      {
         TypeEnum DataType;
         int Enum;
         const char *Key;
      };
      static const Tag tags[] =
      {
         {String,  Title,         "TITLE"},
         {String,  Album,         "ALBUM"},
         {String,  Artist,        "ARTIST"},
         {String,  Accompaniment, "PERFORMER"},
         {String,  Publisher,     "ORGANIZATION"},
         {String,  Genre,         "GENRE"},
         {String,  Isrc,          "ISRC"},

         {Integer, Year,          "DATE"},
         {Integer, Year,          "YEAR"},
         {Integer, Track,         "TRACKNUMBER"}
      };

      auto p = comments[i];
      auto q = strchr(p, '=');
      if (!q)
         continue;
      *q++ = 0;

      auto n = q - p;
      auto length = lengths[i];
      if (n > length)
         continue;
      length -= n;

      for (auto r = p; *r; ++r)
      {
         if (*r >= 'a' && *r <= 'z')
            *r += 'A'-'a';
      }

      for (auto t = tags; t<tags+ARRAY_SIZE(tags); ++t)
      {
         if (!strcmp(t->Key, p))
         {
            switch (t->DataType)
            {
            case String:
               if (recv->OnString)
               {
                  recv->OnString(
                     (StringMetadata)t->Enum,
                     [q, length] (std::string &str, error *err) -> void
                     {
                        try
                        {
                           str = std::string(q, length);
                        }
                        catch (std::bad_alloc)
                        {
                           error_set_nomem(err);
                        }
                     },
                     err
                  );
                  ERROR_CHECK(err);
               }
               break;
            case Integer:
               if (recv->OnInteger)
               {
                  recv->OnInteger(
                     (IntegerMetadata)t->Enum,
                     [q] (int64_t &i, error *err) -> void
                     {
                        char *r = nullptr;
                        i = strtoll(q, &r, 10);
                     },
                     err
                  );
                  ERROR_CHECK(err);
               }
               break;
            }
            break;
         }
      }
   }
exit:;
}
