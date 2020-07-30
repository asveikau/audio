/*
 Copyright (C) 2017-2020 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef audio_device_h
#define audio_device_h

#include "AudioSource.h"
#include <vector>

namespace audio {

// Structure used to determine what sample rates a device will
// support.
//
struct SampleRateSupport
{
   // Range of acceptable rates, or -1.
   // Most devices won't fill this.
   //
   int minRate, maxRate;

   // Some devices have a fixed array of what they'll accept.
   //
   std::vector<int> rates;

   SampleRateSupport() : minRate(-1), maxRate(-1) {}

   // Helper method for common rates seen "in the wild" that we
   // may wish to probe a device about.
   //
   static const int *GetCommonSampleRates()
   {
      static const int rates[] =
      {
         7350, 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000,
         64000, 88200, 96000, 192000,
         0
      };
      return rates;
   }
};

struct Device : public RefCountable
{
   // Programmer-ese string description of the device.
   //
   virtual const char *GetName(error *err) = 0;

   // Attempt to set sample rate, format, etc.
   // Do this first.
   //
   virtual void SetMetadata(const Metadata &md, error *err) = 0;

   // Write samples.
   // @len: Length in bytes
   //
   virtual void Write(const void *buf, int len, error *err) = 0;

   // Some audio drivers (eg. Apple) keep time and want to know if
   // you will be pausing the stream.  Others get the hint if you
   // just cease calling Write().
   //
   virtual void NotifyStop(error *err) {}

   // Query what sample rates the device supports.  Not all devices will
   // return interesting information here.
   //
   virtual void GetSupportedSampleRates(SampleRateSupport &rates, error *err) {}

   // Ask if the device supports a specific rate.  If not, it may offer
   // a nearby suggestion.
   //
   // @rate: Sample rate to be queried
   // @suggestedRate: Out paremeter, MAY be assigned a value other than rate
   //                 for "best fit".
   //
   virtual void ProbeSampleRate(int rate, int &suggestedRate, error *err);
};

struct Mixer : public RefCountable
{
   typedef int value_t;

   virtual int GetValueCount(error *err) = 0;
   virtual const char *DescribeValue(int idx, error *err) = 0;

   virtual int GetChannels(int idx, error *err) = 0;

   // A device can specify values as floats or integers.
   // The default implementation of either one will convert in terms of the
   // other.  So an implementation need only provide integer or float, not both.
   //
   // Integer interface:
   //
   virtual void GetRange(int idx, value_t &min, value_t &max, error *err);
   virtual void SetValue(int idx, const value_t *val, int n, error *err);
   virtual int GetValue(int idx, value_t *value, int n, error *err);
   // Float interface:
   //
   virtual void SetValue(int idx, const float *val, int n, error *err);
   virtual int GetValue(int idx, float *val, int n, error *err);
};

struct DeviceEnumerator : public RefCountable
{
   virtual int GetDeviceCount(error *err) = 0;
   virtual void GetDevice(int idx, Device **output, error *err) = 0;
   virtual void GetDefaultDevice(Device **output, error *err) = 0;

   // The mixer API is new, so may be unsupported in some drivers.
   //
   virtual void GetMixer(int idx, Mixer **output, error *err)
   {
      ERROR_SET(err, notimpl);
   exit:;
   }
   virtual void GetDefaultMixer(Mixer **output, error *err)
   {
      ERROR_SET(err, notimpl);
   exit:;
   }
};

// Internal base class for single-device implementation.
//
struct SingleDeviceEnumerator : public DeviceEnumerator
{
   int GetDeviceCount(error *err) { return 1; }

   void GetDevice(int idx, Device **output, error *err)
   {
      if (idx < 0 || idx >= GetDeviceCount(err))
         ERROR_SET(err, unknown, "Invalid argument");

      GetDefaultDevice(output, err);
   exit:;
   }

   void GetMixer(int idx, struct Mixer **output, error *err)
   {
      if (idx < 0 || idx >= GetDeviceCount(err))
         ERROR_SET(err, unknown, "Invalid argument");

      GetDefaultMixer(output, err);
   exit:;
   }
};

void
GetDeviceEnumerator(DeviceEnumerator **out, error *err);

//
// The following calls shall be considered internal.  Use
// audio::GetDeviceEnumerator instead.
//

void
GetAlsaDeviceEnumerator(DeviceEnumerator **out, error *err);

void
GetCoreAudioDeviceEnumerator(DeviceEnumerator **out, error *err);

void
GetDevAudioDeviceEnumerator(DeviceEnumerator **out, error *err);

void
GetOssDeviceEnumerator(DeviceEnumerator **out, error *err);

void
GetSndioDeviceEnumerator(DeviceEnumerator **out, error *err);

void
GetWasapiDeviceEnumerator(DeviceEnumerator **out, error *err);

void
GetWinMmDeviceEnumerator(DeviceEnumerator **out, error *err);

} // namespace

#endif
