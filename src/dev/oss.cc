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
#include <stdio.h>
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

#include "devnodeenum.h"

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
         fd = open(filename.c_str(), O_NONBLOCK | O_WRONLY);
         if (fd < 0)
            ERROR_SET(err, errno, errno);
         if (fcntl(fd, F_SETFL, 0))
            ERROR_SET(err, errno, errno);
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

class OssEnumerator : public DevNodeEnumerator
{
 protected:
   void
   Open(const char *filename, int fd, Device **out, error *err)
   {
      try
      {
         *out = new OssDev(filename, fd);
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

#if defined(__linux__)
   const char * const *
   GetPossibleSubdirectories()
   {
      static const char * const r[] =
      {
         "sound",
         "snd",
         nullptr,
      };
      return r;
   }
#endif

   const char * const *
   GetPossibleDeviceNodeNames(DevNodeEnumerator::Mode m)
   {
      static const char
      * const
      pcm[] =
      {
         "dsp",
         nullptr,
      },
      * const
      mixer[] =
      {
         "mixer",
         nullptr,
      };
      switch (m)
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

public:

   OssEnumerator()
   {
      openNonBlock = true;
   }

   // FreeBSD does some interesting things with lazily created device
   // nodes, so we can't rely on listing /dev to discover devices as
   // done elsewhere.
   //
#if defined(__FreeBSD__)
   int
   GetDeviceCount(error *err)
   {
      FILE *f = fopen("/dev/sndstat", "r");
      char buf[1024];
      char *p;
      int max = -1;

      if (!f)
         ERROR_SET(err, errno, errno);

      while ((p = fgets(buf, sizeof(buf), f)))
      {
         if (!strncmp(p, "pcm", 3))
         {
            int i;
            if (check_atoi(p+3, i) && i > max)
               max = i;
         }
      }
   exit:
      if (f) fclose(f);
      return max >= 0 ? max+1 : 0;
   }
#endif
};

} // namespace

void
audio::GetOssDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   Pointer<OssEnumerator> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *out = r.Detach();
}

