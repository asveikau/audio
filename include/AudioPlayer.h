/*
 Copyright (C) 2017, 2018, 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef audio_player_h_
#define audio_player_h_

#include "AudioSource.h"
#include "AudioDevice.h"

#include <common/c++/scheduler.h>
#include <common/c++/event.h>

#include <stdlib.h>

struct SpeexResamplerState_;

namespace audio {

struct PlayerVisState;

struct VisualizationArgs
{
   const float *buffer;
   int n;
};

// Time is represented in 100ns units.
//
struct TimeSyncArgs
{
   uint64_t Position, Duration;
};

// The player class implements a ->Step() call which blocks to produce a
// single packet of audio.
//
class Player : public RefCountable
{
   common::Pointer<Device> dev;
   common::Pointer<Source> source;
   Metadata md;
   void *buffer;
   int bufsz;
   uint64_t pos;
   PlayerVisState *visState;
   SpeexResamplerState_ *resampler;
   std::vector<unsigned char> resampleBuffer;
   common::Pointer<common::RefCountable> wakeLock;
   void ProcessVis(const void *buf, int len);
   void TimeSync(error *err);
public:
   Player();
   ~Player();

   // Subscribe to this to be called back periodically with an FFT.
   //
   common::Event<VisualizationArgs> OnVisualizationComputed;

   // Subscribe to this to be called back periodically as time progresses.
   //
   common::Event<TimeSyncArgs> OnTimeSync;

   // Initialize the player.  Call this first.
   // @dev: A specific audio device, or nullptr to use the system default.
   //
   void Initialize(Device *dev, error *err);

   // Should be called after Initialize().
   //
   void SetSource(Source *src, error *err);
   bool HasSource() const { return source.Get() ? true : false; }

   // For PC-like platforms, prevent the system from entering sleep
   // mid-playback.  The wakelock is reference counted.  The idea is
   // that the player maintains its own lock, but a Playlist implementation
   // which may destroy the Player should be able to keep the wakelock
   // alive between tracks.
   //
   //
   void StartWakeLock();
   void BorrowWakeLock(common::RefCountable **p);

   // Duration in 100ns units.
   // XXX: for streaming, this will block until the container knows
   //      how long.  Needs to be addressed at AudioSource layer.
   //
   uint64_t GetDuration(error *err);

   // Current playback position in 100ns units.
   //
   uint64_t GetPosition(error *err);

   // @pos: time in 100ns units
   //
   void Seek(uint64_t pos, error *err);

   // Process a single packet of audio.
   // Returns false for end-of-file.
   //
   bool Step(error *err);

   // Notify the player we intend to pause (no longer call Step()).
   // This will call the Device's NotifyStop() and release the wakelock.
   //
   void NotifyStop(error *err);

   // Block until all current visualization events are processed.
   //
   void SyncVis(error *err);
private:
   void NegotiateMetadata(error *err);
};

// ThreadedPlayer creates a worker thread to call ->Step() on an inner
// player object.  Therefore it has rudimentary play/pause support and
// its calls happen asynchronously.
//
class ThreadedPlayer : public RefCountable
{
   common::Scheduler& scheduler;
   common::Pointer<Player> player;
   bool playing;

   void Schedule(std::function<void(error*)> fn, error *err, bool sync=true);
   void ScheduleStep(error *err);
public:

   // @scheduler: implements worker thread functionality
   //
   ThreadedPlayer(common::Scheduler &scheduler);
   ~ThreadedPlayer();

   // Return true if the worker thread is playing.
   //
   bool IsPlaying() const { return playing; }

   // Called from the worker thread on EOF.
   //
   common::Event<int> TrackCompleted;

   // Wrapping methods in Player...
   //
   common::Event<VisualizationArgs>& GetVisualizationEvent(void);
   common::Event<TimeSyncArgs>& GetTimeSyncEvent(void);
   void Initialize(Device *dev, error *err);
   void SetSource(Source *src, error *err);
   bool HasSource() const { return player.Get() ? player->HasSource() : false; }
   void BorrowWakeLock(common::RefCountable **p)
   {
      if (player.Get())
         player->BorrowWakeLock(p);
      else
         *p = nullptr;
   }

   void Play(error *err);
   void Stop(error *err);

   uint64_t GetDuration(error *err);
   uint64_t GetPosition(error *err);
   void Seek(uint64_t pos, error *err);
};

} // end namespace

#endif
