/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioDevice.h>

#include <alsa/asoundlib.h>

#include <common/logger.h>
#include <common/c++/new.h>
#include <common/misc.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

using namespace common;
using namespace audio;

namespace {

void
error_set_alsa(error *err, int code)
{
   if (!code)
      return;
   error_clear(err);
   memcpy(&err->source, "alsa", MIN(4, sizeof(err->source)));
   err->code = code;
   err->get_string = [] (error *err) -> const char *
   {
      return snd_strerror(err->code);
   };
}

class AlsaDev : public Device
{
   snd_pcm_t *pcm;
   Metadata oldMetadata;

public:
   AlsaDev()
      : pcm(nullptr)
   {
      memset(&oldMetadata, 0, sizeof(oldMetadata));
   }

   ~AlsaDev()
   {
      if (pcm)
      {
         snd_pcm_drain(pcm);
         snd_pcm_close(pcm);
      }
   }

   void
   Initialize(const char *device, error *err)
   {
      int r = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
      if (r)
         ERROR_SET(err, alsa, r);
   exit:;
   }

   const char *GetName(error *err)
   {
      return snd_pcm_name(pcm);
   }

   void SetMetadata(const Metadata &md, error *err)
   {
      snd_pcm_hw_params_t *params = nullptr;
      int r = 0;
      int little_endian = 1;
      snd_pcm_format_t fmt;
      unsigned int rate = md.SampleRate;

      if (oldMetadata.Channels &&
          (oldMetadata.Channels != md.Channels ||
           oldMetadata.SampleRate != md.SampleRate ||
           oldMetadata.Format != md.Format))
      {
         try
         {
            std::string devName = GetName(err);
            ERROR_CHECK(err);

            snd_pcm_drain(pcm);
            snd_pcm_close(pcm);
            pcm = nullptr;

            r = snd_pcm_open(&pcm, devName.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
            if (r)
               ERROR_SET(err, alsa, r);
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

      snd_pcm_hw_params_alloca(&params);
      snd_pcm_hw_params_any(pcm, params);

      r = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
      if (r)
         ERROR_SET(err, alsa, r);

      switch (md.Format)
      {
      case PcmShort:
         fmt = *(char*)&little_endian ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_S16_BE;
         break;
      default:
         ERROR_SET(err, unknown, "Unsupported format");
      }

      r = snd_pcm_hw_params_set_format(
         pcm,
         params,
         fmt
      );
      if (r)
         ERROR_SET(err, alsa, r);

      r = snd_pcm_hw_params_set_channels(pcm, params, md.Channels);
      if (r)
         ERROR_SET(err, alsa, r);

      r = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, nullptr);
      if (r)
         ERROR_SET(err, alsa, r);

      r = snd_pcm_hw_params(pcm, params);
      if (r)
         ERROR_SET(err, alsa, r);

      oldMetadata = md;
   exit:;
   }

   void Write(const void *buf, int len, error *err)
   {
      const auto &md = oldMetadata;
      len /= (md.Channels * GetBitsPerSample(md.Format)/8);
      int r = snd_pcm_writei(pcm, buf, len);
      if (r == -EPIPE)
         snd_pcm_prepare(pcm);
      else if (r < 0)
         ERROR_SET(err, alsa, r);
   exit:;
   }

   bool
   ProbeSampleRate(int rate, int *suggestion, error *err)
   {
      int r = 0;
      snd_pcm_hw_params_t *params = nullptr;
      unsigned rateParam = rate;
      bool probed = false;

      if (!pcm)
      {
         try
         {
            std::string devName = GetName(err);
            r = snd_pcm_open(&pcm, devName.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
            if (r) ERROR_SET(err, unknown, snd_strerror(r));
         }
         catch(std::bad_alloc)
         {
            ERROR_SET(err, nomem);
         }
      }

      snd_pcm_hw_params_alloca(&params);
      snd_pcm_hw_params_any(pcm, params);

      if (!snd_pcm_hw_params_test_rate(pcm, params, rate, 0))
      {
         probed = true;
         goto exit;
      }

      if (suggestion)
      {
         r = snd_pcm_hw_params_set_rate_near(pcm, params, &rateParam, nullptr);
         if (r)
            ERROR_SET(err, alsa, r);
         *suggestion = rateParam;
      }
   exit:
      return probed;
   }

   void
   ProbeSampleRate(int rate, int &suggestion, error *err)
   {
      ProbeSampleRate(rate, &suggestion, err);
   }

   void GetSupportedSampleRates(SampleRateSupport &spec, error *err)
   {
      try
      {
         for (auto p = SampleRateSupport::GetCommonSampleRates(); *p; ++p)
         {
            auto probe = ProbeSampleRate(*p, nullptr, err);
            ERROR_CHECK(err);
            if (probe)
               spec.rates.push_back(*p);
         }
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }
};

void ErrorCallback(
   const char *file,
   int line,
   const char *function,
   int err,
   const char *fmt,
   ...
)
{
   char buf[4096];
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   log_printf(
      "ALSA: at %s:%d, %s: %s: %s",
      file,
      line,
      function,
      snd_strerror(err),
      buf
   );
}

class AlsaEnumerator : public SingleDeviceEnumerator
{
public:
   AlsaEnumerator()
   {
      snd_lib_error_set_handler(ErrorCallback);
   }

   void
   GetDefaultDevice(Device **output, error *err)
   {
      Pointer<AlsaDev> pcm;

      New(pcm, err);
      ERROR_CHECK(err);

      pcm->Initialize(GetDefaultDevice(), err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err))
         pcm = nullptr;
      *output = pcm.Detach();
   }

private:
   const char *
   GetDefaultDevice()
   {
      const char *p = getenv("ALSA_DEFAULT_PCM");
      if (!p)
         p = "default";
      return p;
   }
};

} // namespace

void
audio::GetAlsaDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   Pointer<AlsaEnumerator> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *out = r.Detach();
}

