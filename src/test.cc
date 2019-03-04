/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include <AudioPlayer.h>
#include <common/logger.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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

   if (!*files)
      ERROR_SET(&err, unknown, "Usage: test file [file2 ...]");

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

      audio::OpenCodec(file.Get(), nullptr, src.GetAddressOf(), &err);
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
