/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#import <AudioToolbox/AudioToolbox.h>

#include <common/sem.h>
#include <common/mutex.h>
#include <common/misc.h>
#include <common/c++/lock.h>
#include <common/c++/new.h>

#include <AudioDevice.h>

using namespace common;
using namespace audio;

namespace {

template<bool AutoReset>
class Event
{
   volatile bool state;
   mutex waiterLock;
   struct Waiter
   {
      semaphore *sem;
      struct Waiter *next;
   };
   Waiter *waiters;
public:
   Event() : state(false), waiters(nullptr)
   {
      error err;
      mutex_init(&waiterLock, &err);
      if (ERROR_FAILED(&err))
         throw std::bad_alloc();
   }
   Event(const Event &p) = delete;
   ~Event()
   {
      mutex_destroy(&waiterLock);
   }

   void Set()
   {
      locker lock;

      if (state)
         return;

      state = true;

      lock.acquire(&waiterLock);

      auto p = waiters;
      waiters = nullptr;
      if (AutoReset && p) state = false;
      while (p)
      {
         auto q = p->next;
         sm_post(p->sem);
         p = q;
      }
   }

   void Reset()
   {
      state = false;
   }

   void Wait()
   {
      locker lock;

      if (state)
      {
         if (AutoReset) state = false;
         return;
      }

      Waiter *waiter = new Waiter();
      semaphore sem;
      auto wait = false;

      waiter = new Waiter();
      error err;
      sm_init(&sem, 0, &err);
      assert(!ERROR_FAILED(&err)); // XXX
      waiter->sem = &sem;

      lock.acquire(&waiterLock);
      if (!state)
      {
         waiter->next = waiters;
         waiters = waiter;
         wait = true;
      }
      else if (AutoReset)
      {
         state = false;
      }
      lock.release();

      if (wait)
         sm_wait(&sem);
      sm_destroy(&sem);
      delete waiter;
   }
};

class CoreAudioDevice : public Device
{
   AudioQueueRef queue;
   Event<true> bufferAvailableEvent;
   semaphore bufferConsumedSem;
   AudioQueueBufferRef currentBuffer;
   bool sawBuffer;
   bool started;
   AudioQueueBufferRef starterBuffers[3];
   int nStarterBuffer;
   semaphore shutdownSem;

   void CleanupOld()
   {
      if (queue)
      {
         auto q = queue;

         if (!nStarterBuffer)
         {
            // Flush current buffer.
            //
            sawBuffer = false;
            sm_post(&bufferConsumedSem);

            // Let the callback see an empty buffer so it knows to
            // exit.
            //
            sm_post(&bufferConsumedSem);
            bufferAvailableEvent.Wait();
            sm_wait(&shutdownSem);

            // XXX
            // There is a race where the semaphore may be signalled
            // twice here.  Reset it.
            //
            sm_destroy(&bufferConsumedSem);
            error err;
            sm_init(&bufferConsumedSem, 0, &err);
            assert(!ERROR_FAILED(&err)); // XXX
         }

         started = false;

         while (nStarterBuffer)
         {
            AudioQueueFreeBuffer(q, starterBuffers[--nStarterBuffer]);
            starterBuffers[nStarterBuffer] = nullptr;
         }

         AudioQueueDispose(q, FALSE);
         queue = nullptr;
      }

   }

public:

   CoreAudioDevice() :
      queue(nullptr),
      currentBuffer(nullptr),
      sawBuffer(false),
      started(false),
      nStarterBuffer(0)
   {
      error err;

      memset(&bufferConsumedSem, 0, sizeof(bufferConsumedSem));
      memset(&shutdownSem, 0, sizeof(shutdownSem));

      sm_init(&bufferConsumedSem, 0, &err);
      ERROR_CHECK(&err);
      sm_init(&shutdownSem, 0, &err);
      ERROR_CHECK(&err);

      memset(&starterBuffers, 0, sizeof(starterBuffers));

   exit:
      if (ERROR_FAILED(&err))
      {
         sm_destroy(&bufferConsumedSem);
         sm_destroy(&shutdownSem);
         throw std::bad_alloc();
      }
   }

   ~CoreAudioDevice()
   {
      CleanupOld();
      sm_destroy(&bufferConsumedSem);
      sm_destroy(&shutdownSem);
   }

   const char *GetName(error *error)
   {
      return "CoreAudio";
   }

   void SetMetadata(const Metadata &md, error *err)
   {
      OSStatus status = 0;
      AudioStreamBasicDescription descr = {0};

      CleanupOld();

      switch (md.Format)
      {
      case PcmShort:
         descr.mFormatID = kAudioFormatLinearPCM;
         descr.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
         break;
      default:
         ERROR_SET(err, unknown, "Invalid format");
      }

      descr.mSampleRate = md.SampleRate;
      descr.mChannelsPerFrame = md.Channels;
      descr.mBitsPerChannel = GetBitsPerSample(md.Format); 
      descr.mBytesPerFrame = md.Channels * descr.mBitsPerChannel / 8;
      descr.mBytesPerPacket = descr.mBytesPerFrame;
      descr.mFramesPerPacket = 1;

      status = AudioQueueNewOutput(
         &descr,
         OutputCallbackStatic,
         this,
         nullptr,
         nullptr,
         0,
         &queue
      );
      if (status) ERROR_SET(err, osstatus, status);

      for (; nStarterBuffer<ARRAY_SIZE(starterBuffers); ++nStarterBuffer)
      {
         status = AudioQueueAllocateBuffer(
            queue,
            md.SamplesPerFrame * descr.mBytesPerFrame,
            &starterBuffers[nStarterBuffer]
         );
         if (status) ERROR_SET(err, osstatus, status);
      }

   exit:;
   }

   void OutputCallback(
      AudioQueueRef queue,
      AudioQueueBufferRef buffer
   )
   {
      OSStatus status = 0;
      error err;

      if (queue != this->queue)
         return;

      buffer->mAudioDataByteSize = 0;
      currentBuffer = buffer;
      bufferAvailableEvent.Set();
      sm_wait(&bufferConsumedSem);
      currentBuffer = nullptr;

      if (!buffer->mAudioDataByteSize)
         goto exit;

      status = AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
      if (status) ERROR_SET(&err, osstatus, status); 

      return;
   exit:
      AudioQueueStop(queue, TRUE);
      this->queue = nullptr;
      sm_post(&shutdownSem);
   }

   static void OutputCallbackStatic(
      void *userData,
      AudioQueueRef queue,
      AudioQueueBufferRef buffer
   )
   {
      auto This = reinterpret_cast<CoreAudioDevice*>(userData);
      This->OutputCallback(queue, buffer);
   }

   void NotifyStop(error *err)
   {
      AudioQueuePause(queue);
      started = false;
   }

   void Write(const void *buf, int len, error *err)
   {
      if (!len) return;

   retry:
      auto starter = nStarterBuffer;
      if (starter)
      {
         currentBuffer = starterBuffers[nStarterBuffer-1]; 
      }
      else if (!started)
      {
         AudioQueueStart(queue, nullptr);
         started = true;
      }

      if (!starter && !sawBuffer)
      {
         bufferAvailableEvent.Wait();
         sawBuffer = true;
      } 

      auto buffer = currentBuffer;
      auto available = buffer->mAudioDataBytesCapacity -
                       buffer->mAudioDataByteSize;
      auto fromSoundCard = ((char*)buffer->mAudioData) +
                           buffer->mAudioDataByteSize;
      auto n = MIN(available, len);

      memcpy(fromSoundCard, buf, n);
      buf = ((const char*)buf) + n;
      len -= n;
      fromSoundCard += n;
      buffer->mAudioDataByteSize += n;
      available -= n;

      if (!available)
      {
         if (starter)
         {
            starterBuffers[--nStarterBuffer] = nullptr;
            OSStatus status = AudioQueueEnqueueBuffer(
               queue,
               currentBuffer,
               0,
               nullptr
            );
            if (status) ERROR_SET(err, osstatus, status);
         }
         else
         {
            sm_post(&bufferConsumedSem);
            sawBuffer = false;
         }
      }
      if (len)
         goto retry;

   exit:;
   }
};

class CoreAudioMixer : public Mixer
{
   AudioDeviceID dev;

public:

   CoreAudioMixer()
      : dev(kAudioObjectUnknown)
   {
   }

   void
   Initialize(error *err)
   {
      GetDevice(err);
   }

   int
   GetValueCount(error *err)
   {
      return 1;
   }

   const char *
   DescribeValue(int idx, error *err)
   {
      const char *r = nullptr;
      ValidateIndex(idx, err);
      ERROR_CHECK(err);
      switch (idx)
      {
      case 0:
         r = "vol"; // case and abbrevation matches OSS
         break;
      }
   exit:
      return r;
   }

   int
   GetChannels(int idx, error *err)
   {
      int r = 0;
      ValidateIndex(idx, err);
      ERROR_CHECK(err);
      r = 1;
   exit:
      return r;
   }

   void
   SetValue(int idx, const float *val, int n, error *err)
   {
      AudioObjectPropertyAddress addr = {0};
      OSStatus status = 0;

      GetIndex(idx, &addr, err);

      if (n < 1)
         ERROR_SET(err, unknown, "Buffer too small");

      status = AudioObjectSetPropertyData(dev, &addr, 0, nullptr, sizeof(*val) * n, val);
      if (status)
         ERROR_SET(err, osstatus, status);

   exit:;
   }

   int
   GetValue(int idx, float *val, int n, error *err)
   {
      int r = 0;
      AudioObjectPropertyAddress addr = {0};
      OSStatus status = 0;
      UInt32 sz = 0;

      GetIndex(idx, &addr, err);
      ERROR_CHECK(err);

      if (n < 1)
         ERROR_SET(err, unknown, "Buffer too small");

      sz = sizeof(*val) * n;
      status = AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &sz, val);
      if (status)
         ERROR_SET(err, osstatus, status);

      r = sz/sizeof(*val);

   exit:
      return r;
   }

private:
   void
   ValidateIndex(int idx, error *err)
   {
      if (idx < 0 || (idx >= GetValueCount(err) && !ERROR_FAILED(err)))
         error_set_unknown(err, "Invalid index");
   }

   void
   GetDevice(error *err)
   {
      if (dev == kAudioObjectUnknown)
      {
         AudioObjectPropertyAddress addr = {0};
         auto obj = kAudioObjectSystemObject;

         addr.mScope = kAudioObjectPropertyScopeGlobal;
         addr.mElement = kAudioObjectPropertyElementMaster;
         addr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;

         if (AudioObjectHasProperty(obj, &addr))
         {
            UInt32 sz = sizeof(dev);
            OSStatus status = AudioObjectGetPropertyData(obj, &addr, 0, nullptr, &sz, &dev);
            if (status)
               ERROR_SET(err, osstatus, status);
         }

         if (dev == kAudioObjectUnknown)
            ERROR_SET(err, unknown, "Could not get audio device");
      }
   exit:;
   }

   void
   GetIndex(int idx, AudioObjectPropertyAddress *addr, error *err)
   {
      ValidateIndex(idx, err);
      ERROR_CHECK(err);

      switch (idx)
      {
      case 0:
         addr->mScope = kAudioDevicePropertyScopeOutput;
         addr->mElement = kAudioObjectPropertyElementMaster;
         addr->mSelector = kAudioHardwareServiceDeviceProperty_VirtualMasterVolume;
         break;
      }
   exit:;
   }
};

struct CoreAudioEnumerator : public SingleDeviceEnumerator
{
   void GetDefaultDevice(Device **output, error *err)
   {
      Pointer<CoreAudioDevice> r;

      New(r.GetAddressOf(), err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *output = r.Detach();
   }

   void GetDefaultMixer(Mixer **output, error *err)
   {
      Pointer<CoreAudioMixer> r;

      New(r.GetAddressOf(), err);
      ERROR_CHECK(err);

      r->Initialize(err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *output = r.Detach();
   }
};

} // namespace


void
audio::GetCoreAudioDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   Pointer<CoreAudioEnumerator> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *out = r.Detach();
}

