/*
 Copyright (C) 2017, 2018, 2019 Andrew Sveikauskas

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

#include <sys/audioio.h>
#include <sys/ioctl.h>

#if !defined(AUDIO_ENCODING_SLINEAR)
#define AUDIO_ENCODING_SLINEAR AUDIO_ENCODING_LINEAR
#endif

using namespace common;
using namespace audio;

namespace {

class DevAudioDev : public Device
{
   int fd;
   char nameBuffer[MAX_AUDIO_DEV_LEN * 3 + 4];

public:
   DevAudioDev(int fd_) :
      fd(fd_)
   {
   }

   ~DevAudioDev()
   {
      if (fd >= 0)
         close(fd);
   }

   const char *GetName(error *err)
   {
      struct audio_device info = {0};
      const char *p = nullptr;

      if (ioctl(fd, AUDIO_GETDEV, &info))
         ERROR_SET(err, errno, errno);

      snprintf(
         nameBuffer, sizeof(nameBuffer),
         "%s %s %s",
         info.name,
         info.version,
         info.config
      );
      p = nameBuffer;
   exit:
      return p;
   }

   void SetMetadata(const Metadata &md, error *err)
   {
      struct audio_info info;

      AUDIO_INITINFO(&info);

      info.play.sample_rate = md.SampleRate;
      info.play.channels = md.Channels;

      switch (md.Format)
      {
      case PcmShort:
         info.play.encoding = AUDIO_ENCODING_SLINEAR;
         info.play.precision = 16;
         break;
      default:
         ERROR_SET(err, unknown, "Unknown format");
      }

#if defined(AUMODE_PLAY)
      info.mode = AUMODE_PLAY;
#endif

      if (ioctl(fd, AUDIO_SETINFO, &info))
         ERROR_SET(err, errno, errno);

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

   void
   ProbeSampleRate(int rate, int &suggestion, error *err)
   {
      struct audio_info info;

      AUDIO_INITINFO(&info);

      info.play.sample_rate = rate;

      if (!ioctl(fd, AUDIO_SETINFO, &info) &&
          !ioctl(fd, AUDIO_GETINFO, &info))
      {
         suggestion = info.play.sample_rate;
      }
   }
};

class DevAudioEnumerator : public DeviceEnumerator
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
            *out = new DevAudioDev(fd);
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

      TryOpen("/dev/audio", dev.ReleaseAndGetAddressOf());
      if (dev.Get())
         OnDevice(dev);

#if !defined(__sun__)
      TryOpen("/dev/sound", dev.ReleaseAndGetAddressOf());
      if (dev.Get())
         OnDevice(dev);
#endif
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
audio::GetDevAudioDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   Pointer<DevAudioEnumerator> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

   r->Initialize(err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *out = r.Detach();
}

