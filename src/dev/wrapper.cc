/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioDevice.h>

#include <errno.h>
#include <vector>

#include <common/c++/new.h>

using namespace common;
using namespace audio;

namespace {

struct Entry
{
   void (*Fn)(DeviceEnumerator **, error *);
};

const Entry
Entries[] =
{
#if defined(USE_ALSA)
   { GetAlsaDeviceEnumerator },
#endif
#if defined(USE_COREAUDIO)
   { GetCoreAudioDeviceEnumerator },
#endif
#if defined(USE_DEVAUDIO)
   { GetDevAudioDeviceEnumerator },
#endif
#if defined(USE_OSS)
   { GetOssDeviceEnumerator },
#endif
#if defined(USE_SNDIO)
   { GetSndioDeviceEnumerator },
#endif
#if defined(USE_WASAPI)
   { GetWasapiDeviceEnumerator },
#endif
#if defined(USE_WINMM)
   { GetWinMmDeviceEnumerator },
#endif
   { NULL }
};

class DeviceEnumerationDispatch : public DeviceEnumerator
{
   std::vector<Pointer<Device>> devices;

public:
   void GetDefaultDevice(Device **output, error *err)
   {
      Pointer<DeviceEnumerator> e;
      Pointer<Device> dev;

      for (auto p = Entries; p->Fn && !dev.Get(); ++p)
      {
         p->Fn(e.ReleaseAndGetAddressOf(), err);
         if (!ERROR_FAILED(err) && e.Get())
            e->GetDefaultDevice(dev.ReleaseAndGetAddressOf(), err);
         if (ERROR_FAILED(err))
         {
            dev = nullptr;
            error_clear(err);
         }
      }

      if (!dev.Get())
         ERROR_SET(err, unknown, "Could not open default device");

   exit:
      if (ERROR_FAILED(err))
         dev = nullptr;
      *output = dev.Detach();
   }

   int GetDeviceCount(error *err)
   {
      int r = 0;

      TryLoadDevices(err);
      ERROR_CHECK(err);

      r = devices.size();

   exit:
      return r;
   }

   void GetDevice(int idx, Device **output, error *err)
   {
      Pointer<Device> dev;

      TryLoadDevices(err);
      ERROR_CHECK(err);

      if (idx < 0 || idx >= devices.size())
         ERROR_SET(err, unknown, "Device out of range");

      dev = devices[idx];
   exit:
      *output = dev.Detach();
   }

private:
   void TryLoadDevices(error *err)
   {
      Pointer<DeviceEnumerator> e;

      for (auto p = Entries; p->Fn; ++p)
      {
         p->Fn(e.ReleaseAndGetAddressOf(), err);
         if (ERROR_FAILED(err) || !e.Get())
         {
            error_clear(err);
            continue;
         }
         for (int i=0, count = e->GetDeviceCount(err); i<count; ++i)
         {
            Pointer<Device> dev;
            if (ERROR_FAILED(err))
               break;
            e->GetDevice(i, dev.GetAddressOf(), err);
            if (ERROR_FAILED(err))
               break;
            try
            {
               devices.push_back(dev);
            }
            catch (std::bad_alloc)
            {
               ERROR_SET(err, nomem);
            }
         }
         if (ERROR_FAILED(err))
            error_clear(err);
      }
   exit:;
   }
};

} // end namespace

void
audio::GetDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   DeviceEnumerationDispatch *r = nullptr;
   New(&r, err);
   ERROR_CHECK(err);
exit:
   *out = r;
}

#include <algorithm>

void
audio::Device::ProbeSampleRate(int rate, int &suggestion, error *err)
{
   SampleRateSupport spec;

   GetSupportedSampleRates(spec, err);
   ERROR_CHECK(err);

   // No specific rates to set?
   //
   if (!spec.rates.size())
   {
      // Did we get back a range?
      //
      if (spec.minRate > 0 && spec.maxRate > 0)
      {
         if (rate < spec.minRate)
            rate = spec.minRate;
         else if (rate > spec.maxRate)
            rate = spec.maxRate;

         // Fall through ...
      }

      // Either we validated the range above, or maybe we can't query rates
      // with this driver.  Use whatever rate we have now.
      //
      suggestion = rate;
      goto exit;
   }

   std::sort(spec.rates.begin(), spec.rates.end());

   // Look for the chosen rate, or the first higher one.
   //
   for (auto p : spec.rates)
   {
      if (p >= rate)
      {
         suggestion = p;
         goto exit;
      }
   }

   // Input rate is higher than all supported, pick the highest.
   //
   suggestion = spec.rates[spec.rates.size() - 1];
exit:;
}
