/*
 Copyright (C) 2017-2020, 2022 Andrew Sveikauskas

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
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <string>
#include <algorithm>

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

   void
   GetSupportedFormats(const Format *&formats, int &n, error *err)
   {
      static const Format workingFormats[] =
      {
         PcmShort,
         Pcm24,
      };
      formats = workingFormats;
      n = ARRAY_SIZE(workingFormats);
   }

   void SetMetadata(const Metadata &md, error *err)
   {
#if defined(AUDIO_SETINFO)
      // NetBSD, Sun
      //
      struct audio_info info;

      AUDIO_INITINFO(&info);

      info.play.sample_rate = md.SampleRate;
      info.play.channels = md.Channels;

      info.play.encoding = AUDIO_ENCODING_SLINEAR;
      switch (md.Format)
      {
      case PcmShort:
         info.play.precision = 16;
         break;
      case Pcm24:
         info.play.precision = 24;
         break;
      default:
         ERROR_SET(err, unknown, "Unknown format");
      }

#if defined(AUMODE_PLAY)
      info.mode = AUMODE_PLAY;
#endif

      if (ioctl(fd, AUDIO_SETINFO, &info))
         ERROR_SET(err, errno, errno);
#else
      // OpenBSD
      //
      struct audio_swpar par;

      AUDIO_INITPAR(&par);

      par.rate = md.SampleRate;
      par.pchan = md.Channels;

      switch (md.Format)
      {
      case PcmShort:
         par.bits = 16;
         break;
      case Pcm24:
         par.bits = 24;
         break;
      default:
         ERROR_SET(err, unknown, "Unknown format");
      }

      par.sig = 1;
      par.le = *(unsigned char*)(&par.sig);
      par.msb = 1;
      par.bps = par.bits/8;

      if (ioctl(fd, AUDIO_SETPAR, &par))
         ERROR_SET(err, errno, errno);
#endif
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
#if defined(AUDIO_SETINFO)
      struct audio_info info;

      AUDIO_INITINFO(&info);

      info.play.sample_rate = rate;

      if (!ioctl(fd, AUDIO_SETINFO, &info) &&
          !ioctl(fd, AUDIO_GETINFO, &info))
      {
         suggestion = info.play.sample_rate;
      }
#else
      struct audio_swpar par;

      AUDIO_INITPAR(&par);

      par.rate = rate;

      if (!ioctl(fd, AUDIO_SETPAR, &par) &&
          !ioctl(fd, AUDIO_GETPAR, &par))
      {
         suggestion = par.rate;
      }
#endif
   }
};

#if defined(AUDIO_MIXER_DEVINFO)
#define HAVE_DEV_MIXER 1

class DevMixer : public SoftMuteMixer
{
   int fd;
   std::vector<mixer_devinfo> devinfo;
   struct field
   {
      char name[64];
      mixer_devinfo *info;

      void
      setName(const char *str)
      {
         size_t slen = strlen(str);
         size_t dst = MIN(sizeof(name)-1, slen);
         memcpy(name, str, dst);
         name[dst] = 0;
      }
   };
   std::vector<field> fields;
public:
   DevMixer(int fd_) : fd(fd_) {}
   ~DevMixer() { if (fd >= 0) close(fd); }

   bool
   Initialize(error *err)
   {
      bool r = true;
      struct mixer_devinfo info = {0};
      std::vector<field> rawFields;
      int masterIdx = 0;

      for (info.index = 0; !ioctl(fd, AUDIO_MIXER_DEVINFO, &info); ++info.index)
      {
         try
         {
            devinfo.push_back(info);
         }
         catch (const std::bad_alloc&)
         {
            ERROR_SET(err, nomem);
         }
      }
      if (!devinfo.size())
      {
         r = false;
         goto exit;
      }

      // First load all device info structs.
      //
      for (int i=0; i<devinfo.size(); ++i)
      {
         field f;
         f.info = &devinfo[i];
         f.setName(f.info->label.name);
         try
         {
            rawFields.push_back(f);
         }
         catch (const std::bad_alloc&)
         {
            ERROR_SET(err, nomem);
         }
      }

      // Now we filter through and add string prefixes to complicated values,
      // inspired by OpenBSD's mixerctl(1).
      //
      for (int i=0; i<devinfo.size(); ++i)
      {
         auto &info = devinfo[i];
         if (info.type != AUDIO_MIXER_CLASS &&
             info.prev == AUDIO_MIXER_LAST)
         {
            try
            {
               fields.push_back(rawFields[i]);
               for (int j=info.next; j != AUDIO_MIXER_LAST; j=devinfo[j].next)
               {
                  std::string str = rawFields[i].name;
                  str += '.';
                  str += devinfo[j].label.name;
                  rawFields[j].setName(str.c_str());
                  fields.push_back(rawFields[j]);
               }
            }
            catch (const std::bad_alloc&)
            {
               ERROR_SET(err, nomem);
            }
         }
      }
      for (int i=0; i<fields.size(); ++i)
      {
         auto cl = fields[i].info->mixer_class;
         if (cl >= 0 && cl < devinfo.size())
         {
            try
            {
               std::string str = devinfo[cl].label.name;
               str += '.';
               str += fields[i].name;
               fields[i].setName(str.c_str());
            }
            catch (const std::bad_alloc&)
            {
               ERROR_SET(err, nomem);
            }
         }
      }

      // Filter out less interesting fields.
      //
      fields.erase(std::remove_if(fields.begin(), fields.end(),
         [] (field &f) -> bool
         {
            if (f.info->type != AUDIO_MIXER_VALUE)
               return true;

            static const char record[] = "record.";
            if (!strncmp(f.name, record, sizeof(record)-1))
               return true;

            return false;
         }), fields.end());

      // Make sure "outputs.master" appears first, as this is what happens
      // in the other drivers.
      //
      for (auto &f : fields)
      {
         if (!strcmp(f.name, "outputs.master"))
         {
            masterIdx = &f - fields.data();
            break;
         }
      }
      if (masterIdx != 0)
         std::swap(fields[0], fields[masterIdx]);
   exit:
      return r;
   }

   int
   GetValueCount(error *err)
   {
      return fields.size();
   }

   const char *
   DescribeValue(int idx, error *err)
   {
      if (idx < 0 || idx >= fields.size())
         ERROR_SET(err, unknown, "Invalid index");
      return fields[idx].name;
   exit:
      return nullptr;
   }

   int
   GetChannels(int idx, error *err)
   {
      if (idx < 0 || idx >= fields.size())
         ERROR_SET(err, unknown, "Invalid index");
      return fields[idx].info->un.v.num_channels;
   exit:
      return 0;
   }

   void
   GetRange(int idx, value_t &min, value_t &max, error *err)
   {
      min = 0;
      max = 255;
   }

   void
   SetValue(int idx, const value_t *value, int n, error *err)
   {
      mixer_ctrl ctrl = {0};

      if (idx < 0 || idx >= fields.size())
         ERROR_SET(err, unknown, "Invalid index");
      if (n < 0)
         ERROR_SET(err, unknown, "Invalid size");
      if (!n)
         goto exit;

      ctrl.dev = fields[idx].info->index;
      ctrl.dev = fields[idx].info->type;

      ctrl.un.value.num_channels = MIN(ARRAY_SIZE(ctrl.un.value.level), n);
      for (auto i = 0; i<ctrl.un.value.num_channels; ++i)
         ctrl.un.value.level[i] = value[i];

      if (ioctl(fd, AUDIO_MIXER_WRITE, &ctrl))
         ERROR_SET(err, errno, errno);
   exit:;
   }

   int
   GetValue(int idx, value_t *value, int n, error *err)
   {
      mixer_ctrl ctrl = {0};
      int r = 0;

      if (idx < 0 || idx >= fields.size())
         ERROR_SET(err, unknown, "Invalid index");
      if (n < 0)
         ERROR_SET(err, unknown, "Invalid size");
      if (!n)
         goto exit;

      ctrl.dev = fields[idx].info->index;
      ctrl.dev = fields[idx].info->type;
      ctrl.un.value.num_channels = MIN(n, fields[idx].info->un.v.num_channels);

      if (ioctl(fd, AUDIO_MIXER_READ, &ctrl))
         ERROR_SET(err, errno, errno);

      r = MIN(n, ctrl.un.value.num_channels);
      for (auto i = 0; i<r; ++i)
         value[i] = ctrl.un.value.level[i];
   exit:
      return r;
   }
};
#endif

#if defined(AUDIO_SETINFO)
#define HAVE_AUDIO_CTL 1

class AudioCtlMixer : public SoftMuteMixer
{
   int fd;
public:
   AudioCtlMixer(int fd_) : fd(fd_) {}
   ~AudioCtlMixer() { if (fd >= 0) close(fd); }

   int
   GetValueCount(error *err)
   {
      return 1;
   }

   const char *
   DescribeValue(int idx, error *err)
   {
      switch (idx)
      {
      case 0: return "vol";
      default:
         error_set_unknown(err, "Invalid index");
         return nullptr;
      }
   }

   int
   GetChannels(int idx, error *err)
   {
      if (idx < 0 || idx >= GetValueCount(err))
      {
         error_set_unknown(err, "Invalid index");
         return 0;
      }

      return 1;
   }

   void
   GetRange(int idx, value_t &min, value_t &max, error *err)
   {
      if (idx < 0 || idx >= GetValueCount(err))
      {
         error_set_unknown(err, "Invalid index");
         return;
      }

      min = 0;
      max = 255;
   }

   void
   SetValue(int idx, const value_t *value, int n, error *err)
   {
      struct audio_info info;

      if (idx < 0 || idx >= GetValueCount(err))
         ERROR_SET(err, unknown, "Invalid index");
      if (n < 0)
         ERROR_SET(err, unknown, "Invalid count");
      if (!n)
         goto exit;

      AUDIO_INITINFO(&info);
      info.play.gain = *value;
      if (ioctl(fd, AUDIO_SETINFO, &info))
         ERROR_SET(err, errno, errno);
   exit:;
   }

   int
   GetValue(int idx, value_t *value, int n, error *err)
   {
      struct audio_info info;

      if (idx < 0 || idx >= GetValueCount(err))
         ERROR_SET(err, unknown, "Invalid index");
      if (n < 0)
         ERROR_SET(err, unknown, "Invalid count");
      if (!n)
         goto exit;

      AUDIO_INITINFO(&info);
      if (ioctl(fd, AUDIO_GETINFO, &info))
         ERROR_SET(err, errno, errno);
      *value = info.play.gain;
      return 1;
   exit:
      return 0;
   }
};

#endif

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
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   void
   Open(const char *filename, int fd, struct Mixer **out, error *err)
   {
      Pointer<struct Mixer> r;

      // On Solaris, /dev/mixer is an alias for sndstat.
      //
#if defined(__sun__)
      if (!strcmp(filename, "/dev/mixer"))
         goto exit;
#endif

      try
      {
#if defined(HAVE_DEV_MIXER)
         {
            auto rp = new DevMixer(fd);
            r = rp;
            if (!rp->Initialize(err))
               r = nullptr;
            ERROR_CHECK(err);
            if (r.Get())
               goto exit;
         }
#endif
#if defined(HAVE_AUDIO_CTL)
         {
            auto rp = new AudioCtlMixer(fd); 
            r = rp;
            goto exit;
         }
#endif
      }
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }
exit:
      if (ERROR_FAILED(err))
         r = nullptr;
      *out = r.Detach();
   }

   const char * const *
   GetPossibleDeviceNodeNames(DevNodeEnumerator::Mode mode)
   {
      static const char
      * const
      pcm[] =
      {
         "audio",
#if defined(__NetBSD__)
         "sound",
#endif
         nullptr,
      },
      * const
      mixer[] =
      {
#if defined(HAVE_DEV_MIXER)
         "mixer",
#endif
#if defined(HAVE_AUDIO_CTL)
         "audioctl",
#endif
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

