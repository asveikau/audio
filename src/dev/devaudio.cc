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

#include "devnodeenum.h"

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

class DevAudioEnumerator : public DevNodeEnumerator
{
protected:
   void
   Open(const char *filename, int fd, Device **out, error *err)
   {
      try
      {
         *out = new DevAudioDev(fd);
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   const char * const *
   GetPossibleDeviceNodeNames(DevNodeEnumerator::Mode mode)
   {
      static const char
      * const
      pcm[] =
      {
         "audio",
#if !defined(__sun__)
         "sound",
#endif
         nullptr,
      },
      * const
      mixer[] =
      {
         "audioctl",
         nullptr
      };
      switch (mode)
      {
      case Pcm:
         return pcm;
      case Mixer:
         return mixer;
      case ConsiderEnvironment:
         break;
      }
      return nullptr;
   }
};

} // namespace

void
audio::GetDevAudioDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   Pointer<DevAudioEnumerator> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *out = r.Detach();
}

