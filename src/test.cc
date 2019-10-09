/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#define __STDC_FORMAT_MACROS 1

#include <AudioCodec.h>
#include <AudioPlayer.h>
#include <common/logger.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#if defined(_WINDOWS) && !defined(PRId64)
#define PRId64 "I64d"
#endif

#if defined(_WINDOWS)
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
   log_register_callback(
      [] (void *np, const char *p) -> void { fputs(p, stderr); },
      nullptr
   );
   error err;
   common::Pointer<common::Stream> file;
   common::Pointer<audio::Source> src;
   common::Pointer<audio::Player> player;
   FILE *f = nullptr;
   auto files = argv + 1;
   audio::MetadataReceiver recv;

   if (!*files)
      ERROR_SET(&err, unknown, "Usage: test file [file2 ...]");

   recv.OnString = [] (audio::StringMetadata type, const std::function<void(std::string&, error*)> &parse, error *err) -> void
   {
      std::string str;
      parse(str, err);
      if (!ERROR_FAILED(err) && str.length())
      {
         log_printf("Metadata: %s = %s", ToString(type), str.c_str());
      }
   };
   recv.OnInteger = [] (audio::IntegerMetadata type, const std::function<void(int64_t&, error*)> &parse, error *err) -> void
   {
      int64_t i = 0;
      parse(i, err);
      if (!ERROR_FAILED(err))
      {
         log_printf("Metadata: %s = %" PRId64, ToString(type), i);
      }
   };
   recv.OnBinaryData = [] (audio::BinaryMetadata type, const std::function<void(common::Stream**, error*)> &parse, error *err) -> void
   {
      log_printf("Metadata: binary data: %s", ToString(type));
   };

   audio::RegisterCodecs();

   *player.GetAddressOf() = new audio::Player();
   player->Initialize(nullptr, &err);
   ERROR_CHECK(&err);

   while (*files)
   {
      auto filename = *files++;
#if defined(_WINDOWS)
      f = _wfopen(filename, L"rb");
#else
      f = fopen(filename, "rb");
#endif
      if (!f) ERROR_SET(&err, errno, errno);

      common::CreateStream(f, file.GetAddressOf(), &err);
      ERROR_CHECK(&err); 

      f = nullptr;

      audio::CodecArgs args;
      args.Metadata = &recv;

      audio::OpenCodec(file.Get(), &args, src.GetAddressOf(), &err);
      ERROR_CHECK(&err); 

      file = nullptr;   

      player->SetSource(src.Get(), &err);
      ERROR_CHECK(&err); 

      src = nullptr;

      while (player->Step(&err));

      error_clear(&err);
   }

exit:
   if (f) fclose(f);
}
