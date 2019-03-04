/*
 Copyright (C) 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioDevice.h>
#include <common/c++/new.h>

#include <sndio.h>

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
