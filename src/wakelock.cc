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

struct WakeLock : public common::RefCountable
{
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
   common::WorkerThread *w;

   WakeLock() : w(nullptr) {}

   void
   Initialize(error *err)
   {
      common::New(&w, err);
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

   ~WakeLock()
   {
      if (w)
      {
         w->Schedule(
            [] (error *err) -> void
            {
               SetThreadExecutionState(ES_CONTINUOUS);
            }
         );
         delete w;
      }
   }
#endif
};

template<typename T>
class WeakCache : public common::RefCountable
{
   volatile T *cache;
   std::mutex m;
public:
   WeakCache() : cache(nullptr) {}

   bool
   TryGet(T** p)
   {
      *p = nullptr;

      if (cache)
      {
         common::locker l;
         l.acquire(m);
         if ((*p = (T*)cache))
         {
            (*p)->AddRef();
         }
      }
      return (*p) ? true : false;
   }

   void
   TryDelete(T *ptr)
   {
      common::locker l;

      l.acquire(m);
      if (ptr->Release() && cache == ptr)
         cache = nullptr;
   }

   void
   TryPut(T *ptr)
   {
      common::locker l;

      l.acquire(m);
      cache = ptr;
   }
};

static
common::Pointer<WeakCache<WakeLock>> cache;
static
lazy_init_state cacheInit;

struct WakeLockWrapper : public common::RefCountable
{
   WakeLock *p;
   common::Pointer<WeakCache<WakeLock>> cache;

   WakeLockWrapper() : p(nullptr) {}
   ~WakeLockWrapper() { if (p) cache->TryDelete(p); }

   void
   Initialize(error *err)
   {
      lazy_init(
         &cacheInit,
         [] (void *context, error *err) -> void
         {
            New(::cache.GetAddressOf(), err);
         },
         nullptr,
         err
      );
      ERROR_CHECK(err);
      cache = ::cache;

      if (!cache->TryGet(&p))
      {
         New(&p, err);
         ERROR_CHECK(err);

         p->Initialize(err);
         ERROR_CHECK(err);

         cache->TryPut(p);
      }
   exit:;
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
