/*
 Copyright (C) 2017, 2018, 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioPlayer.h>

#include <common/logger.h>
#include <common/misc.h>
#include <common/time.h>
#include <common/c++/worker.h>
#include <common/c++/new.h>

#include "wakelock.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <tools/kiss_fftr.h>

#define OUTSIDE_SPEEX
#define RANDOM_PREFIX libaudio
#include "../../third_party/libspeex-resample/speex_resampler.h"

using namespace common;
using namespace audio;

#if !defined(_WINDOWS)
#include <unistd.h>
#define Sleep(x) usleep((x) * 1000)
#include <pthread.h>
#if defined(__linux__)
#include <sched.h>
#endif
#endif

namespace audio {
struct PlayerVisState
{
   std::vector<signed char> pendingPacket;
   WorkerThread *thread;
   float *buffer;
   int n;
   kiss_fftr_cfg fftr;
   int fftr_n;
   kiss_fft_cpx *cpx;

   PlayerVisState() :
      thread(nullptr),
      buffer(nullptr),
      n(0),
      fftr(nullptr),
      fftr_n(0),
      cpx(nullptr)
   {
   }
   ~PlayerVisState()
   {
      if (thread) delete thread;
      if (fftr) kiss_fftr_free(fftr);
      if (buffer) delete[] buffer;
      if (cpx) delete[] cpx;
   }
};
} // end namespace

audio::Player::Player()
  : buffer(nullptr), bufsz(0), pos(0), resampler(nullptr)
{
   memset(&md, 0, sizeof(md));
   visState = new PlayerVisState;
} 

audio::Player::~Player()
{
   if (buffer)
      delete [] (char*)buffer;
   delete visState;
   if (resampler)
      speex_resampler_destroy(resampler);
}

void
audio::Player::SyncVis(error *err)
{
   if (visState->thread)
   {
      delete visState->thread;
      visState->thread = nullptr;
   }
}

void
audio::Player::NotifyStop(error *err)
{
   // Allow some output drivers (eg. CoreAudio) to pause playback.
   //
   dev->NotifyStop(err);

   // The computer can go to sleep if it wants.  Clear wakelock.
   //
   wakeLock = nullptr;
}

void
audio::Player::StartWakeLock()
{
   error err;

   if (!wakeLock.Get())
   {
      CreateWakeLock(wakeLock.GetAddressOf(), &err);
      ERROR_CHECK(&err);
   }

exit:;
}

void
audio::Player::BorrowWakeLock(common::RefCountable **p)
{
   auto q = wakeLock.Get();
   if (q)
      q->AddRef();
   *p = q;
}

static void
LogMetadata(const Metadata &md, const char *descr)
{
   char frameSize[16];
   char channels[32];
   const char *channelsString = channels;
   const char *frameSizeString = "default";

   if (md.SamplesPerFrame)
   {
      snprintf(frameSize, sizeof(frameSize), "%d", md.SamplesPerFrame);
      frameSizeString = frameSize;
   }

   switch (md.Channels)
   {
   case 1:
      channelsString = "mono";
      break;
   case 2:
      channelsString = "stereo";
      break;
   default:
      snprintf(channels, sizeof(channels), "%d channels", md.Channels); 
   }

   if (descr && !*descr)
      descr = nullptr;

   log_printf(
      "%s%s%s, %d Hz, %s samples per packet, %s",
      descr ? descr : "",
      descr ? (descr[strlen(descr)-1] == ']' ? " " : ", ") : "", 
      channelsString,
      md.SampleRate,
      frameSizeString,
      GetFormatName(md.Format)
   );

}

void
audio::Player::NegotiateMetadata(error *err)
{
   int newBufsz;
   int suggested;
   int restore = 0;

   source->GetMetadata(&md, err);
   ERROR_CHECK(err);

   LogMetadata(md, source->Describe());

   if (!md.SamplesPerFrame)
   {
      md.SamplesPerFrame = 20 * md.SampleRate / 1000;
   }

   suggested = md.SampleRate;
   dev->ProbeSampleRate(md.SampleRate, suggested, err);
   ERROR_CHECK(err);

   if (resampler)
   {
      speex_resampler_destroy(resampler);
      resampler = nullptr;
   }

   if (suggested != md.SampleRate)
   {
      log_printf(
         "Device suggests resample from %d Hz to %d Hz",
         md.SampleRate, suggested
      );

      int speexErr = 0;
      resampler = speex_resampler_init(
         md.Channels,
         md.SampleRate,
         suggested,
         10,
         &speexErr
      );
      if (!resampler || speexErr)
      {
         log_printf("resampler init fail; p=%p, err=%d", resampler, speexErr);
         ERROR_SET(err, unknown, "Resampler init fail");
      }
      restore = md.SampleRate;
      md.SampleRate = suggested;
   }
   else if (resampleBuffer.size())
   {
      resampleBuffer.resize(0);
      resampleBuffer.shrink_to_fit();
   }

   dev->SetMetadata(md, err);
   ERROR_CHECK(err);

   if (restore)
      md.SampleRate = restore;

   newBufsz = md.SamplesPerFrame * md.Channels *
              GetBitsPerSample(md.Format) / 8;
   if (buffer && newBufsz < bufsz)
   {
      bufsz = newBufsz;
      goto exit;
   }

   if (buffer)
   {
      delete [] (char*)buffer;
      buffer = nullptr;
      bufsz = 0;
   }

   try
   {
      buffer = new char[newBufsz];
   }
   catch (const std::bad_alloc&)
   {
      ERROR_SET(err, nomem);
   }

   bufsz = newBufsz;
exit:;
}

void
audio::Player::ProcessVis(const void *buf, int len)
{
   const int packetMs = 80;
   int samplesPerPacket = md.SampleRate * packetMs / 1000;
   int n;
   auto &pendingPacket = visState->pendingPacket;

retry:
   n = len / (GetBitsPerSample(md.Format) / 8) / md.Channels;

   if (n > samplesPerPacket - pendingPacket.size())
   {
      int m = md.Channels * GetBitsPerSample(md.Format) / 8
                          * (samplesPerPacket - pendingPacket.size());
      ProcessVis(buf, m);
      buf = (const char*)buf + m;
      len -= m;
      goto retry;
   }

   try
   {
      switch (md.Format)
      {
      case PcmShort:
         {
            const int16_t *p = (const int16_t*)buf;
            for (int i=0; i<n; ++i)
            {
               float f = *p++;
               for (int j = 1; j<md.Channels; ++j)
                  f += *p++;
               f /= (md.Channels * 32767.0f);
               pendingPacket.push_back(f * 127.0f);
            }
         }
         break;
      default:
         return;
      }

      if (!visState->thread)
         visState->thread = new WorkerThread();
   }
   catch (const std::bad_alloc&)
   {
      pendingPacket.resize(0);
      return;
   }

   if (pendingPacket.size() != samplesPerPacket)
      return;

   n = samplesPerPacket;

   signed char *dst = pendingPacket.data();
   const int divisor = 16;
   for (signed char *src = dst;
        src < pendingPacket.data() + n/divisor*divisor;)
   {
      int i = *src++;
      for (int j = 1; j<divisor; ++j)
         i += *src++;
      *dst++ = i/divisor;
   }
   pendingPacket.resize(dst - pendingPacket.data());
   n = pendingPacket.size();
   n &= ~1;

   visState->thread->Schedule(
      [this, pendingPacket, n, packetMs] (error *err) -> void
      {
         if (n > visState->n)
         {
            if (visState->buffer) delete [] visState->buffer;
            if (visState->cpx) delete [] visState->cpx;
            visState->buffer = new (std::nothrow) float[n];
            visState->cpx = new (std::nothrow) kiss_fft_cpx[n/2+1];
            if (!visState->buffer || !visState->cpx)
            {
               visState->n = 0;
               return;
            }
            visState->n = n;
         }
      
         if (visState->fftr_n != n)
         {
            if (visState->fftr) kiss_fftr_free(visState->fftr);
            visState->fftr = kiss_fftr_alloc(n, 0, nullptr, nullptr);
            if (!visState->fftr)
            {
               visState->fftr_n = 0;
               return;
            }
            visState->fftr_n = n;
         }
      
         float *samples = visState->buffer;
      
         for (int i=0; i<n; ++i)
         {
            samples[i] = (pendingPacket[i] + 0.0f) / 127.0f;
         }
      
         kiss_fftr(visState->fftr, visState->buffer, visState->cpx);
      
         VisualizationArgs r;
         r.buffer = visState->buffer;
         r.n = n/2 + 1;
         float max = 0.0f;
         for (int i=0; i<r.n; ++i)
         {
            visState->buffer[i] = fabs(visState->cpx[i].r);
            if (i > r.n/20 && visState->buffer[i] > max)
               max = visState->buffer[i];
         }
         if (max != 0)
            for (int i=0; i<r.n; ++i)
               visState->buffer[i] /= max;
         auto pct = r.n/20;
         if (r.n > pct*2)
         {
            r.buffer += pct;
            r.n -= pct * 2;
         }
         OnVisualizationComputed.Invoke(r);

         static uint64_t delta;
         int delay = packetMs;
         if (delta)
         {
            int dd = MIN(delta, delay);
            delay -= dd;
            delta -= dd;
         }
         if (delay)
         {
            auto start = get_monotonic_time_millis();
            Sleep(delay);
            auto actual = get_monotonic_time_millis() - start;
            if (actual > delay)
               delta += actual - delay;
         }
      }
   );

   pendingPacket.resize(0);
}

bool
audio::Player::Step(error *err)
{
   bool cont = true;
   int r = 0;

   StartWakeLock();

   r = source->Read(buffer, bufsz, err);
   ERROR_CHECK(err);

   if (r)
   {
      if (OnVisualizationComputed.HasSubscribers())
      {
         ProcessVis(buffer, r);
      }

      if (resampler)
      {
         int denom = GetBitsPerSample(md.Format) / 8 * md.Channels;
         spx_uint32_t inLen = r / denom;
         spx_uint32_t outLen = 0;

         spx_uint32_t rate_in, rate_out;
         speex_resampler_get_rate(resampler, &rate_in, &rate_out);

         int desiredSize = (int64_t)r * rate_out / rate_in;
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
         int speexErr =
            speex_resampler_process_interleaved_int(
               resampler,
               (const spx_int16_t*)buffer,
               &inLen,
               (spx_int16_t*)resampleBuffer.data(),
               &outLen
            );
         if (speexErr)
         {
            log_printf("resampler returned %d", speexErr);
            ERROR_SET(err, unknown, "Resampler error");
         }

         dev->Write(resampleBuffer.data(), outLen * denom, err);
      }
      else
      {
         dev->Write(buffer, r, err);
      }

      if (ERROR_FAILED(err))
      {
         error innerError;
         log_printf("Device write error, trying to re-open...");
         dev = nullptr;
         Initialize(nullptr, &innerError);
         if (!ERROR_FAILED(&innerError))
            source->Seek(pos, &innerError);
         if (!innerError.source)
         {
            source->MetadataChanged = false;
            NegotiateMetadata(&innerError);
         }
         if (!ERROR_FAILED(&innerError))
            error_clear(err);
         ERROR_CHECK(err);
         goto exit;
      }

      pos += (0ULL + r / (md.Channels * GetBitsPerSample(md.Format) / 8))
             * 10000000LL / md.SampleRate;

      TimeSync(err);

      ERROR_CHECK(err);
   }

   if (source->MetadataChanged)
   {
      NegotiateMetadata(err);
      ERROR_CHECK(err);
      source->MetadataChanged = false;
      goto exit;
   }

   cont = (r != 0);
exit:
   return cont && !ERROR_FAILED(err);
}

void
audio::Player::Initialize(Device *dev, error *err)
{
   const char *devname = nullptr;

   if (dev)
      this->dev = dev;
   else
   {
      Pointer<DeviceEnumerator> enumerator;

      GetDeviceEnumerator(enumerator.GetAddressOf(), err);
      ERROR_CHECK(err);

      enumerator->GetDefaultDevice(this->dev.ReleaseAndGetAddressOf(), err);
      ERROR_CHECK(err);
   }

   if (!this->dev.Get())
      ERROR_SET(err, unknown, "No device.");

   this->dev->NotifyStop(err);
   ERROR_CHECK(err);

   devname = this->dev->GetName(err);
   error_clear(err);

   if (devname)
   {
      log_printf("device: %s", devname);
   }
exit:;
}

void
audio::Player::SetSource(Source *src, error *err)
{
   this->source = src;
   if (src)
   {
      NegotiateMetadata(err);
      ERROR_CHECK(err);
      pos = src->GetPosition(err);
      ERROR_CHECK(err);
   }
exit:;
}

void
audio::Player::Seek(uint64_t pos, error *err)
{
   if (!source.Get()) goto exit;
   source->Seek(pos, err);
   ERROR_CHECK(err);
   this->pos = source->GetPosition(err);
   ERROR_CHECK(err);
   TimeSync(err);
   ERROR_CHECK(err);
exit:;
}

uint64_t
audio::Player::GetPosition(error *err)
{
   return pos;
}

uint64_t
audio::Player::GetDuration(error *err)
{
   return source.Get() ? source->GetDuration(err) : 0ULL;
}

void
audio::Player::TimeSync(error *err)
{
    if (OnTimeSync.HasSubscribers())
    {
       TimeSyncArgs args;
       args.Position = pos;
       args.Duration = GetDuration(err);
       ERROR_CHECK(err);
       OnTimeSync.Invoke(args, err);
       ERROR_CHECK(err);
    }
exit:;
}

//
// Threaded player
//

audio::ThreadedPlayer::ThreadedPlayer(Scheduler &sched)
   : scheduler(sched), playing(false)
{
}

audio::ThreadedPlayer::~ThreadedPlayer()
{
   error err;
   Stop(&err);
   // Need synchronous schedule to prevent blowup...
   Schedule([] (error *err) -> void {}, &err);
}

void
audio::ThreadedPlayer::Schedule(
   const std::function<void(error*)> &fn, error *err, bool sync
)
{
   scheduler.Schedule(
      fn,
      sync,
      err
   );
}

void
audio::ThreadedPlayer::ScheduleStep(error *err)
{
   if (!playing)
      return;

   scheduler.Schedule(
      [this] (error *err) -> void
      {
         if (!playing || !player.Get())
            return;

         playing = player->Step(err) && !ERROR_FAILED(err);
         if (!playing)
            TrackCompleted.Invoke(1);

         ScheduleStep(err);
      },
      false, // async
      err
   );
}

void
audio::ThreadedPlayer::Initialize(Device *dev, error *err)
{
   New(player.ReleaseAndGetAddressOf(), err);
   ERROR_CHECK(err);

   player->Initialize(dev, err);
   ERROR_CHECK(err);

   Schedule(
      [] (error *err) -> void
      {
#if defined(_WINDOWS)
         SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
         struct sched_param param = {0};
         int pol = 0;
         int r = 0;

         r = pthread_getschedparam(pthread_self(), &pol, &param);
         if (r)
            ERROR_SET(err, errno, errno);

         pol = SCHED_FIFO;

         for (;;)
         {
            param.sched_priority = sched_get_priority_max(pol);

            r = pthread_setschedparam(pthread_self(), pol, &param);

#if defined(__FreeBSD__) || defined(__NetBSD__)
            if (r && pol != SCHED_OTHER)
            {
               pol = SCHED_OTHER;
               continue;
            }
#endif

            break;
         }

         if (r)
            ERROR_SET(err, errno, errno);
      exit:
         error_clear(err);
#endif
      },
      err
   );
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) player = nullptr;
}

common::Event<VisualizationArgs>&
audio::ThreadedPlayer::GetVisualizationEvent(void)
{
   return player->OnVisualizationComputed;
}

common::Event<TimeSyncArgs>&
audio::ThreadedPlayer::GetTimeSyncEvent(void)
{
   return player->OnTimeSync;
}

void
audio::ThreadedPlayer::SetSource(Source *src, error *err)
{
   Schedule(
      [this, src] (error *err) -> void
      {
         player->SetSource(src, err);
      },
      err
   );
}

void
audio::ThreadedPlayer::Play(error *err)
{
   Schedule(
      [this] (error *err) -> void
      {
         if (!playing)
         {
            playing = true;
            if (player.Get())
               player->StartWakeLock();
            ScheduleStep(err);
         }
      },
      err
   );
}

void
audio::ThreadedPlayer::Stop(error *err)
{
   // Need to capture these for proper reference counting in
   // async operation...
   //
   auto &player = this->player;
   auto &playing = this->playing;

   if (!player.Get())
      return;

   Schedule(
      [&playing, player] (error *err) -> void
      {
         player->SyncVis(err);
         playing = false;
         player->NotifyStop(err);
      },
      err,
      false
   );
}

uint64_t
audio::ThreadedPlayer::GetDuration(error *err)
{
   uint64_t r = 0;
   Schedule(
      [this, &r] (error *err) -> void
      {
         r = player->GetDuration(err);
      },
      err
   );
   return r;
}

uint64_t
audio::ThreadedPlayer::GetPosition(error *err)
{
   uint64_t r = 0;
   Schedule(
      [this, &r] (error *err) -> void
      {
         r = player->GetPosition(err);
      },
      err
   );
   return r;
}

void
audio::ThreadedPlayer::Seek(uint64_t pos, error *err)
{
   Schedule(
      [this, pos] (error *err) -> void
      {
         player->Seek(pos, err);
      },
      err
   );
}

