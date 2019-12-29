/*
 Copyright (C) 2019 Andrew Sveikauskas

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
   int ndevs;

   common::Pointer<audio::DeviceEnumerator> devEnum;

   audio::GetDeviceEnumerator(devEnum.GetAddressOf(), &err);
   ERROR_CHECK(&err);

   ndevs = devEnum->GetDeviceCount(&err);
   ERROR_CHECK(&err);

   printf("%d audio devices.\n", ndevs);

   for (int i=0; i<ndevs; ++i)
   {
      common::Pointer<audio::Device> dev;
      const char *descr = nullptr;

      devEnum->GetDevice(i, dev.GetAddressOf(), &err);
      if (ERROR_FAILED(&err))
      {
         error_clear(&err);
         descr = "Failed to open";
      }
      else
      {
         descr = dev->GetName(&err);
         if (ERROR_FAILED(&err))
         {
            error_clear(&err);
            descr = "Failed to get name";
         }
      }

      printf("Device %d: %s\n", i, descr);
   }

exit:
   return ERROR_FAILED(&err) ? 1 : 0;
}
