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
   std::vector<Pointer<DeviceEnumerator>> enumerators;

public:
   void
   Initialize(error *err)
   {
      Pointer<DeviceEnumerator> e;

      for (auto p = Entries; p->Fn; ++p)
      {
         p->Fn(e.ReleaseAndGetAddressOf(), err);
         error_clear(err);
         if (!e.Get())
            continue;

         try
         {
            enumerators.push_back(e);
         }
         catch (std::bad_alloc)
         {
            ERROR_SET(err, nomem);
         }
      }
   exit:;
   }

   void GetDefaultDevice(Device **output, error *err)
   {
      Pointer<Device> dev;

      for (auto &e : enumerators)
      {
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

      for (auto &e : enumerators)
      {
         int r2 = e->GetDeviceCount(err);
         if (ERROR_FAILED(err))
            error_clear(err);
         else
            r += r2;
      };

      return r;
   }

   void GetDevice(int idx, Device **output, error *err)
   {
      Pointer<Device> dev;

      if (idx < 0)
         ERROR_SET(err, unknown, "Device out of range");

      for (auto &e : enumerators)
      {
         int count = e->GetDeviceCount(err);
         if (ERROR_FAILED(err))
         {
            error_clear(err);
            continue;
         }
         if (idx < count)
         {
            e->GetDevice(idx, dev.GetAddressOf(), err);
            ERROR_CHECK(err);
            goto exit;
         }
         idx -= count;
      }

      ERROR_SET(err, unknown, "Device out of range");

   exit:
      *output = dev.Detach();
   }
};

} // end namespace

void
audio::GetDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   DeviceEnumerationDispatch *r = nullptr;
   New(&r, err);
   ERROR_CHECK(err);
   r->Initialize(err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err) && r)
   {
      delete r;
      r = nullptr;
   }
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
