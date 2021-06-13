/*
 Copyright (C) 2021 Andrew Sveikauskas

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

   common::Pointer<audio::DeviceEnumerator> devEnum;
   common::Pointer<audio::Mixer> dev;

   if (argc != 3)
      ERROR_SET(&err, unknown, "usage: mixer <idx> <0|1>");

   audio::GetDeviceEnumerator(devEnum.GetAddressOf(), &err);
   ERROR_CHECK(&err);

   devEnum->GetDefaultMixer(dev.GetAddressOf(), &err);
   ERROR_CHECK(&err);

#if defined(_WINDOWS)
#define atoi _wtoi // XXX
#endif

   dev->SetMute(atoi(argv[1]), atoi(argv[2]) ? true : false, &err);
   ERROR_CHECK(&err);
exit:;
}
