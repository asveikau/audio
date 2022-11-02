/*
 Copyright (C) 2017, 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioTransform.h>

#include <stdint.h>

#include <vector>

#include <common/logger.h>

#define OUTSIDE_SPEEX
#define RANDOM_PREFIX libaudio
#include "../../third_party/libspeex-resample/speex_resampler.h"

using namespace common;
using namespace audio;

namespace {

struct ResamplerTransform : public Transform
{
   SpeexResamplerState *resampler;
   Metadata md;
   std::vector<unsigned char> resampleBuffer;

   ResamplerTransform() : resampler(nullptr) {}
   ResamplerTransform(const ResamplerTransform & other) = delete;
   ~ResamplerTransform()
   {
      if (resampler)
         speex_resampler_destroy(resampler);
   }

   void
   Initialize(Metadata &md, int newSampleRate, error *err)
   {
      this->md = md;

      int speexErr = 0;
      resampler = speex_resampler_init(
         md.Channels,
         md.SampleRate,
         newSampleRate,
         10,
         &speexErr
      );
      if (!resampler || speexErr)
      {
         log_printf("resampler init fail; p=%p, err=%d", resampler, speexErr);
         ERROR_SET(err, unknown, "Resampler init fail");
      }
      md.SampleRate = newSampleRate;
   exit:;
   }

   void
   TransformAudioPacket(void *&buf, size_t &len, error *err)
   {
      int denom = GetBitsPerSample(md.Format) / 8 * md.Channels;
      spx_uint32_t inLen = len / denom;
      spx_uint32_t outLen = 0;
      int speexErr = 0;

      spx_uint32_t rate_in, rate_out;
      speex_resampler_get_rate(resampler, &rate_in, &rate_out);

      int desiredSize = (int64_t)len * rate_out / rate_in;
      desiredSize = (desiredSize + denom - 1) / denom * denom;

      if (desiredSize > resampleBuffer.size())
      {
         try
         {
            resampleBuffer.resize(desiredSize);
         }
         catch (const std::bad_alloc&)
         {
            ERROR_SET(err, nomem);
         }
      }

      outLen = desiredSize / denom;
      speexErr =
         speex_resampler_process_interleaved_int(
            resampler,
            (const spx_int16_t*)buf,
            &inLen,
            (spx_int16_t*)resampleBuffer.data(),
            &outLen
         );
      if (speexErr)
      {
         log_printf("resampler returned %d", speexErr);
         ERROR_SET(err, unknown, "Resampler error");
      }

      buf = resampleBuffer.data();
      len = outLen * denom;
   exit:;
   }
};

} // namespace

Transform*
audio::CreateResampler(
   Metadata &md,
   int newSampleRate,
   error *err
)
{
   auto r = new (std::nothrow) ResamplerTransform();
   if (!r)
      ERROR_SET(err, nomem);
   r->Initialize(md, newSampleRate, err);
   if (ERROR_FAILED(err))
   {
      delete r;
      r = nullptr;
   }
exit:
   return r;
}
