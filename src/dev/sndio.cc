/*
 Copyright (C) 2018, 2021 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioDevice.h>
#include <common/c++/new.h>

#include <sndio.h>

#include <map>

using namespace common;
using namespace audio;

namespace {

struct SndioDevice : public Device
{
   struct sio_hdl *sndio;
   bool started;

   SndioDevice() :
      sndio(nullptr),
      started(false)
   {
   }

   ~SndioDevice()
   {
      if (sndio)
      {
         if (started)
            sio_stop(sndio);
         sio_close(sndio);
      }
   }

   const char *GetName(error *err)
   {
      return "sndio";
   }

   void Open(error *err)
   {
      sndio = sio_open(SIO_DEVANY, SIO_PLAY, 0);
      if (!sndio)
         ERROR_SET(err, unknown, "sio_open failed");
   exit:;
   }

   void SetMetadata(const Metadata &md, error *err)
   {
      struct sio_par par;

      NotifyStop(err);
      ERROR_CHECK(err);

      sio_initpar(&par);

      switch (md.Format)
      {
      case PcmShort:
         par.bits = 16;
         par.sig = 1;
         par.le = SIO_LE_NATIVE;
         break;
      default:
         ERROR_SET(err, unknown, "Unsupported format");
      }

      par.bps = SIO_BPS(par.bits);

      par.pchan = md.Channels;
      par.rate = md.SampleRate;

      par.xrun = SIO_IGNORE;

      if (!sio_setpar(sndio, &par))
         ERROR_SET(err, unknown, "sio_setpar failed");

   exit:;
   }

   void Write(const void *buf, int len, error *err)
   {
      if (!started)
      {
         if (!sio_start(sndio))
            ERROR_SET(err, unknown, "sio_start failed");
         started = true;
      }

      if (sio_write(sndio, buf, len) != len)
         ERROR_SET(err, unknown, "sndio: short write");
      if (sio_eof(sndio))
         ERROR_SET(err, unknown, "sndio: eof after write");
   exit:;
   }

   void NotifyStop(error *err)
   {
      if (started)
      {
         if (!sio_stop(sndio))
            ERROR_SET(err, unknown, "sio_stop failed");

         started = false;
      }
   exit:;
   }

   void ProbeSampleRate(int rate, int &suggestedRate, error *err)
   {
      Metadata md;
      struct sio_par par;

      md.Format = PcmShort;
      md.Channels = 1;
      md.SampleRate = rate;
      md.SamplesPerFrame = 0;

      SetMetadata(md, err);
      ERROR_CHECK(err);

      if (!sio_getpar(sndio, &par))
         ERROR_SET(err, unknown, "sio_getpar failed");

      suggestedRate = par.rate;
   exit:;
   }
};

struct SndioMixer : public Mixer
{
   sioctl_hdl *mixer;
   std::vector<sioctl_desc> desc;
   std::map<unsigned int, int> desc_by_addr;
   std::map<unsigned int, unsigned int> values_by_addr;

   SndioMixer() : mixer(nullptr) {}
   ~SndioMixer()
   {
      if (mixer)
         sioctl_close(mixer);
   }

   void Initialize(error *err)
   {
      mixer = sioctl_open(SIO_DEVANY, SIO_PLAY, 0);
      if (!mixer)
         ERROR_SET(err, unknown, "sioctl_open failed");
      sioctl_ondesc(
         mixer,
         [] (void *arg, sioctl_desc *desc, int val) -> void
         {
            auto thisp = (SndioMixer*)arg;
            error err;
            thisp->OnDesc(desc, val, &err);
         },
         this
      );
      sioctl_onval(
         mixer,
         [] (void *arg, unsigned int addr, unsigned int val) -> void
         {
            auto thisp = (SndioMixer*)arg;
            error err;
            thisp->OnValue(addr, val, &err);
         },
         this
      );
   exit:;
   }

   void OnDesc(sioctl_desc *desc, int val, error *err)
   {
      if (!desc)
         return;

      try
      {
         auto p = desc_by_addr.find(desc->addr);
         if (p == desc_by_addr.end())
         {
            this->desc.push_back(*desc);
            this->desc_by_addr[desc->addr] = this->desc.size();
         }
         else
         {
            this->desc[p->second] = *desc;
         }
      }
      catch (const std::bad_alloc &)
      {
         ERROR_SET(err, nomem);
      }

      OnValue(desc->addr, val, err);
      ERROR_CHECK(err);
   exit:;
   }

   void OnValue(unsigned int addr, unsigned int val, error *err)
   {
      try
      {
         values_by_addr[addr] = val;
      }
      catch (const std::bad_alloc &)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   int GetValueCount(error *err)
   {
      return desc.size();
   }

   const char *DescribeValue(int idx, error *err)
   {
      if (idx < 0 || idx >= desc.size())
         ERROR_SET(err, unknown, "Invalid index");

      return desc[idx].node0.name;
   exit:
      return nullptr;
   }

   int GetChannels(int idx, error *err)
   {
      return 1;
   }

   void GetRange(int idx, value_t &min, value_t &max, error *err)
   {
      if (idx < 0 || idx >= desc.size())
         ERROR_SET(err, unknown, "Invalid index");
      min = 0;
      max = desc[idx].maxval;
   exit:;
   }

   void SetValue(int idx, const value_t *val, int n, error *err)
   {
      if (idx < 0 || idx >= desc.size())
         ERROR_SET(err, unknown, "Invalid index");
      if (n != 1)
         ERROR_SET(err, unknown, "Expected single value");

      sioctl_setval(mixer, desc[idx].addr, *val);
   exit:;
   }

   int GetValue(int idx, value_t *value, int n, error *err)
   {
      if (idx < 0 || idx >= desc.size())
         ERROR_SET(err, unknown, "Invalid index");
      if (n < 1)
         ERROR_SET(err, unknown, "Not enough room for result");

      {
         auto p = values_by_addr.find(desc[idx].addr);
         if (p == values_by_addr.end())
            ERROR_SET(err, unknown, "Value not found");

         *value = p->second;
         return 1;
      }
   exit:
      return 0;
   }
};

struct SndioDeviceEnumerator : public SingleDeviceEnumerator
{
   void GetDefaultDevice(Device **output, error *err)
   {
      Pointer<SndioDevice> r;

      New(r.GetAddressOf(), err);
      ERROR_CHECK(err);

      r->Open(err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *output = r.Detach();
   }

   void GetDefaultMixer(Mixer **output, error *err)
   {
      Pointer<SndioMixer> r;

      New(r.GetAddressOf(), err);
      ERROR_CHECK(err);

      r->Initialize(err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *output = r.Detach();
   }
};

} // end namespace

void
audio::GetSndioDeviceEnumerator(
   DeviceEnumerator **e,
   error *err
)
{
   Pointer<SndioDeviceEnumerator> r;
   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err))
      r = nullptr;
   *e = r.Detach();
}
