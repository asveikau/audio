/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <common/c++/new.h>

#include <AudioDevice.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <string>

#include <sys/soundcard.h>
#include <sys/ioctl.h>

using namespace common;
using namespace audio;

#if !defined(SNDCTL_DSP_HALT_OUTPUT)
#define SNDCTL_DSP_HALT_OUTPUT SNDCTL_COPR_HALT
#endif

namespace {

class OssDev : public Device
{
   int fd;
   char *nameBuffer;
   Metadata oldMetadata;
   std::string filename;

public:
   OssDev(const char *filename_, int fd_) :
      fd(fd_),
      nameBuffer(nullptr),
      filename(filename_)
   {
      memset(&oldMetadata, 0, sizeof(oldMetadata));
   }

   ~OssDev()
   {
      if (fd >= 0)
         close(fd);
      free(nameBuffer);
   }

   const char *GetName(error *err)
   {
#if defined(SNDCTL_AUDIOINFO)
      oss_audioinfo info;

      if (nameBuffer)
         goto exit;

      memset(&info, 0, sizeof(info));
      if (ioctl(fd, SNDCTL_AUDIOINFO, &info))
         ERROR_SET(err, errno, errno);

      nameBuffer = strdup(info.name);
      if (!nameBuffer)
         ERROR_SET(err, nomem);

   exit:
      return nameBuffer;
#else
      return filename.c_str();
#endif
   }

   void
   GetSupportedSampleRates(SampleRateSupport &spec, error *err)
   {
#if defined(SNDCTL_AUDIOINFO)
      oss_audioinfo info;
      memset(&info, 0, sizeof(info));
      if (ioctl(fd, SNDCTL_AUDIOINFO, &info))
         ERROR_SET(err, errno, errno);

      spec.minRate = info.min_rate;
      spec.maxRate = info.max_rate;

      try
      {
         for (int i=0; i<info.nrates; ++i)
         {
            int rate = info.rates[i];
            if (rate >= spec.minRate && rate <= spec.maxRate)
            {
               spec.rates.push_back(rate);
            }
         }
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
#endif
   }

   void SetMetadata(const Metadata &md, error *err)
   {
      int i;
      int little_endian = 1;

      if (oldMetadata.Channels &&
          (oldMetadata.Channels != md.Channels ||
           oldMetadata.SampleRate != md.SampleRate ||
           oldMetadata.Format != md.Format))
      {
         close(fd);
         fd = -1;
         try
         {
            fd = open(filename.c_str(), O_WRONLY);
            if (fd < 0)
               ERROR_SET(err, errno, errno);
         }
         catch (std::bad_alloc)
         {
            ERROR_SET(err, nomem);
         }
      }
      else if (oldMetadata.Channels)
      {
         // Metadata the same as before?  Ok..
         //
         goto exit;
      }

      i = md.Channels;
      if (ioctl(fd, SNDCTL_DSP_CHANNELS, &i))
         ERROR_SET(err, errno, errno); 

      switch (md.Format)
      {
      case PcmShort:
         i = *(char*)&little_endian ? AFMT_S16_LE : AFMT_S16_BE;
         break;
      default:
         ERROR_SET(err, unknown, "Unknown format");
      }

      if (ioctl(fd, SNDCTL_DSP_SETFMT, &i))
         ERROR_SET(err, errno, errno); 

      i = md.SampleRate;
      if (ioctl(fd, SNDCTL_DSP_SPEED, &i))
         ERROR_SET(err, errno, errno); 

      oldMetadata = md;
   exit:;
   }

   void Write(const void *buf, int len, error *err)
   {
      int r = write(fd, buf, len);
      if (r < 0)
         ERROR_SET(err, errno, errno);
      else if (r != len)
         ERROR_SET(err, unknown, "Short write");
   exit:;
   }
};

class OssEnumerator : public DeviceEnumerator
{
   std::vector<Pointer<Device>> devs;
   Pointer<Device> defaultDev;

   void TryOpen(const char *filename, Device **out)
   {
      int fd = open(filename, O_WRONLY);
      if (fd >= 0)
      {
         try
         {
            *out = new OssDev(filename, fd);
            fd = -1;
         }
         catch (std::bad_alloc)
         {
         }
      }
      if (fd >= 0)
      {
         close(fd);
      }
   }

   void OnDevice(const Pointer<Device> &dev)
   {
      devs.push_back(dev);
      if (!defaultDev.Get())
         defaultDev = dev;
   }

public:

   void Initialize(error *err)
   {
      Pointer<Device> dev;
      const char *env;

      env = getenv("AUDIODEV");
      if (env)
         TryOpen(env, dev.ReleaseAndGetAddressOf());
      if (dev.Get())
         OnDevice(dev);

      TryOpen("/dev/dsp", dev.ReleaseAndGetAddressOf());
      if (dev.Get())
         OnDevice(dev);
   }

   int GetDeviceCount(error *err)
   {
      return devs.size();
   }

   void GetDevice(int idx, Device **output, error *err)
   {
      Pointer<Device> dev;

      if (idx < 0 || idx >= devs.size())
         ERROR_SET(err, errno, EINVAL);

      dev = devs[idx]; 

   exit:;
      *output = dev.Detach();
   }

   void GetDefaultDevice(Device **output, error *err)
   {
      Pointer<Device> r = defaultDev;
      if (!r.Get() && devs.size())
         r = devs[0];
      *output = r.Detach();
   }
};

} // namespace

void
audio::GetOssDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   Pointer<OssEnumerator> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

   r->Initialize(err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *out = r.Detach();
}

