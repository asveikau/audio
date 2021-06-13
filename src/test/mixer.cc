/*
 Copyright (C) 2020 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioDevice.h>
#include <common/logger.h>
#include <stdio.h>

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
   int count;

   common::Pointer<audio::DeviceEnumerator> devEnum;
   common::Pointer<audio::Mixer> dev;

   audio::GetDeviceEnumerator(devEnum.GetAddressOf(), &err);
   ERROR_CHECK(&err);

   devEnum->GetDefaultMixer(dev.GetAddressOf(), &err);
   ERROR_CHECK(&err);

   count = dev->GetValueCount(&err);
   ERROR_CHECK(&err);

   for (int i=0; i<count; ++i)
   {
      auto descr = dev->DescribeValue(i, &err);
      ERROR_CHECK(&err);

      auto channels = dev->GetChannels(i, &err);
      ERROR_CHECK(&err);

#if defined(_MSC_VER)
      float *f = (float*)_alloca(channels * sizeof(float));
#else
      float f[channels];
#endif

      dev->GetValue(i, f, channels, &err);
      ERROR_CHECK(&err);

      printf("%s:", descr);

      for (int i=0; i<channels; ++i)
      {
         auto &fv = f[i];
         printf(" %d", (int)(fv * 100));
      }

      error inner;
      auto flags = dev->GetMuteState(i, &inner);
      if ((flags & audio::MuteState::SoftMute))
         ;
      else if ((flags & audio::MuteState::Muted))
         printf(" [muted]");
      else if ((flags & audio::MuteState::CanMute))
         printf(" [can mute]");

      puts("");
   }

exit:
   return ERROR_FAILED(&err) ? 1 : 0;
}
