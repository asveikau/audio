/*
 Copyright (C) 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include "wakelock.h"
#include <common/c++/new.h>
#include <common/lazy.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && !TARGET_OS_IPHONE
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(_WINDOWS)
#include <common/c++/worker.h>
#else
#define OMIT_WAKELOCK
#endif

#ifndef OMIT_WAKELOCK

#include <mutex>
#include <common/c++/lock.h>

namespace {

struct WakeLock
{
   WakeLock(const WakeLock&) = delete;

#if defined(__APPLE__)
   IOPMAssertionID id;
   bool valid;

   WakeLock() : valid(false) {}

   void
   Initialize(error *err)
   {
      IOReturn r =
         IOPMAssertionCreateWithName(
            kIOPMAssertionTypePreventUserIdleSystemSleep,
            kIOPMAssertionLevelOn,
            CFSTR("Audio playback"),
            &id
         );
      if (r != kIOReturnSuccess)
      {
         ERROR_SET(err, darwin, r);
      }
      valid = true;
   exit:;
   }

   ~WakeLock()
   {
      if (valid)
         IOPMAssertionRelease(id);
   }
#endif

#if defined(_WINDOWS)
   std::unique_ptr<common::WorkerThread> w;

   void
   Initialize(error *err)
   {
      common::New(w, err);
      ERROR_CHECK(err);

      w->Schedule(
         [] (error *err) -> void
         {
            const EXECUTION_STATE fl = ES_CONTINUOUS | ES_SYSTEM_REQUIRED;
            if (!SetThreadExecutionState(fl | ES_AWAYMODE_REQUIRED) &&
                !SetThreadExecutionState(fl))
               ERROR_SET(err, win32, GetLastError());
         exit:;
         },
         true, // synchronous
         err
      );
      ERROR_CHECK(err);

   exit:;
   }

   WakeLock() {}

   ~WakeLock()
   {
      if (w.get())
      {
         w->Schedule(
            [] (error *err) -> void
            {
               SetThreadExecutionState(ES_CONTINUOUS);
            }
         );
      }
   }

#endif
};

template<typename T>
class WeakCache
{
   std::weak_ptr<T> cache;
   std::mutex m;
public:

   WeakCache() {}
   WeakCache(const WeakCache &) = delete;

   template<typename Fn>
   void
   Get(std::shared_ptr<T> &p, Fn create, error *err)
   {
      if (!(p = cache.lock()))
      {
         common::locker l;
         l.acquire(m);

         if ((p = cache.lock()))
            goto exit;

         auto q = create(err);
         ERROR_CHECK(err);
         cache = p = std::move(q);
      }
   exit:;
   }
};

struct WakeLockWrapper : public common::RefCountable
{
   std::shared_ptr<WakeLock> p;

   void
   Initialize(error *err)
   {
      static WeakCache<WakeLock> cache;

      cache.Get(
         p,
         [] (error *err) -> std::shared_ptr<WakeLock>
         {
            std::shared_ptr<WakeLock> r;
            common::New(r, err);
            ERROR_CHECK(err);
            r->Initialize(err);
            ERROR_CHECK(err);
         exit:
            return r;
         },
         err
      );
   }
};

} // end namespace
#endif

void
audio::CreateWakeLock(
   common::RefCountable **ref,
   error *err
)
{
#ifndef OMIT_WAKELOCK
   common::Pointer<WakeLockWrapper> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

   r->Initialize(err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err))
      r = nullptr;
   *ref = r.Detach();
#else
   *ref = nullptr;
#endif
}
