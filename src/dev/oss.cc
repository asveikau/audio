/*
 Copyright (C) 2017-2018, 2020-2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <common/c++/new.h>
#include <common/misc.h>

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
   GetSupportedFormats(const Format *&formats, int &n, error *err)
   {
      //
      // Ideally we would use the GETFMTS ioctl, which works on Linux and FreeBSD.
      // However this tells us only about native capabilities of the device, not
      // any conversion that the OSS layer might do, so this is somewhat pointless
      // at the moment.
      //
      static const Format workingFormats[] =
      {
         PcmShort,
#if defined(AFMT_S24_LE)  // Present in FreeBSD, missing in Linux.
         Pcm24,
#endif
      };
      formats = workingFormats;
      n = ARRAY_SIZE(workingFormats);
   }

#if defined(SNDCTL_DSP_GET_CHNORDER)
   int GetChannelMap(ChannelInfo *info, int n, error *err)
   {
      uint64_t map = 0;
      int r = 0;
      if (ioctl(fd, SNDCTL_DSP_GET_CHNORDER, &map))
         ERROR_SET(err, errno, errno);
      for (int i=0; i<16; ++i)
      {
         ChannelInfo chan = Unknown;
         int dev = (map) & 0xf;
         map >>= 4;

         if (!dev)
            break;

         if (!n)
            ERROR_SET(err, unknown, "Out of buffer space");

         switch (dev)
         {
         case CHID_L:
            chan = FrontLeft;
            break;
         case CHID_R:
            chan = FrontRight;
            break;
         case CHID_C:
            chan = FrontCenter;
            break;
         case CHID_LFE:
            chan = LFE;
            break;
         case CHID_LS:
            chan = SideLeft;
            break;
         case CHID_RS:
            chan = SideRight;
            break;
         case CHID_LR:
            chan = RearLeft;
            break;
         case CHID_RR:
            chan = RearRight;
            break;
         }

         *info++ = chan;
         --n;
         ++r;
      }
   exit:
      if (ERROR_FAILED(err))
         r = 0;
      return r;
   }

#endif

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
      catch (const std::bad_alloc&)
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
#if defined(AFMT_S24_LE)
      case Pcm24:
         i = *(char*)&little_endian ? AFMT_S24_LE : AFMT_S24_BE;
         break;
#endif
#if defined(AFMT_S32_LE) && 0 // Appears broken on FreeBSD
      case Pcm24Pad:
         i = *(char*)&little_endian ? AFMT_S32_LE : AFMT_S32_BE;
         break;
#endif
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

class OssMixer : public SoftMuteMixer
{
   int fd;
   bool enumOccurred;
   unsigned int devMask;
   unsigned int stereoMask;

   void
   TryEnumInfo(error *err)
   {
      if (!enumOccurred)
      {
         if (ioctl(fd, SOUND_MIXER_READ_DEVMASK, &devMask) ||
             ioctl(fd, SOUND_MIXER_READ_STEREODEVS, &stereoMask))
            ERROR_SET(err, errno, errno);

         enumOccurred = true;
      }
   exit:;
   }

   int
   FindDev(int idx)
   {
      if (idx < 0 || idx >= sizeof(devMask)*8)
         return -1;
      for (int i=0; i<sizeof(devMask)*8; ++i)
      {
         if ((devMask & (1U << i)))
         {
            if (idx == 0)
               return i;
            --idx;
         }
      }
      return -1;
   }

public:

   OssMixer(const char *filename, int fd_)
      : fd(fd_), enumOccurred(false)
   {
   }

   ~OssMixer()
   {
      if (fd >= 0)
         close(fd);
   }

   int
   GetValueCount(error *err)
   {
      int r = 0;
      TryEnumInfo(err);
      ERROR_CHECK(err);

      r = __builtin_popcount(devMask);
   exit:
      return r;
   }

   const char *
   DescribeValue(int idx, error *err)
   {
      const char *r = nullptr;
      static const char *labels[] = SOUND_DEVICE_NAMES;

      TryEnumInfo(err);
      ERROR_CHECK(err);

      if ((idx = FindDev(idx)) < 0)
         ERROR_SET(err, unknown, "Invalid index");
      else if (idx >= 0 && idx < ARRAY_SIZE(labels))
         r = labels[idx];
      else
         r = "unknown";
   exit:
      return r;
   }

   int
   GetChannels(int idx, error *err)
   {
      TryEnumInfo(err);
      ERROR_CHECK(err);

      if ((idx = FindDev(idx)) < 0)
         ERROR_SET(err, unknown, "Invalid index");

      return (stereoMask & (1U << idx)) ? 2 : 1;
   exit:
      return 0;
   }

   void
   GetRange(int idx, value_t &min, value_t &max, error *err)
   {
      min = 0;
      max = 100;
   }

   void
   SetValue(int idx, const value_t *val, int n, error *err)
   {
      int ival;

      TryEnumInfo(err);
      ERROR_CHECK(err);

      if ((idx = FindDev(idx)) < 0)
         ERROR_SET(err, unknown, "Invalid index");

      if (!n)
         goto exit;
      if (n < 0)
         ERROR_SET(err, unknown, "Invalid byte count");

      if ((stereoMask & (1U << idx)) && n >= 2)
         ival = (val[1] << 8) | val[0];
      else if (n == 1)
         ival = (val[0] << 8) | val[0];
      else
         ERROR_SET(err, unknown, "Invalid channel setup");

      if (ioctl(fd, MIXER_WRITE(idx), &ival))
         ERROR_SET(err, errno, errno);

   exit:;
   }

   int
   GetValue(int idx, value_t *value, int n, error *err)
   {
      int r = 0;
      int ival;

      TryEnumInfo(err);
      ERROR_CHECK(err);

      if ((idx = FindDev(idx)) < 0)
         ERROR_SET(err, unknown, "Invalid index");

      if (!n)
         goto exit;
      if (n < 0)
         ERROR_SET(err, unknown, "Invalid byte count");

      if (ioctl(fd, MIXER_READ(idx), &ival))
         ERROR_SET(err, errno, errno);

      if ((stereoMask & (1U << idx)))
      {
         if (n >= 2)
         {
            value[0] = (ival & 0xff);
            value[1] = (ival >> 8) & 0xff;
            r = 2;
         }
         else if (n == 1)
         {
            // The sample is in stereo but the caller only asked for one
            // channel.  Take an average.
            //
            value[0] = ((ival & 0xff) + ((ival >> 8) & 0xff)) / 2;
            r = 1;
         }
         else
         {
            ERROR_SET(err, unknown, "Invalid channel setup");
         }
      }
      else if (n >= 1)
      {
         value[0] = (ival & 0xff);
         r = 1;
      }
      else
      {
         ERROR_SET(err, unknown, "Invalid channel setup");
      }

   exit:
      return r;
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
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   void
   Open(const char *filename, int fd, struct Mixer **out, error *err)
   {
      try
      {
         *out = new OssMixer(filename, fd);
      }
      catch (const std::bad_alloc&)
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
            if (check_atoi(p+3, &i) && i > max)
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

