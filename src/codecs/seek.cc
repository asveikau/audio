/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioSource.h>
#include "seekbase.h"

#include <errno.h>

using namespace common;
using namespace audio;

audio::SeekBase::SeekBase(uint64_t duration)
   : cachedDuration(duration)
{
}

void
audio::SeekBase::Seek(uint64_t pos, error *err)
{
   uint64_t currentPos = GetPosition();
   uint64_t nextPos = currentPos + GetNextDuration();
   uint64_t duration = 0;

   if (pos >= currentPos && pos <= nextPos)
      return;

   if (pos < currentPos)
   {
      SeekToStart(err);
      ERROR_CHECK(err);
   }

   while (GetPosition() + (duration = GetNextDuration()) < pos)
   {
      if (!duration)
         return;

      SkipFrame(err); 
      ERROR_CHECK(err);
   }

exit:;
}

uint64_t
audio::SeekBase::GetDuration(error *err)
{
   if (cachedDuration)
      return cachedDuration;

   uint64_t r = GetPosition();
   uint64_t frame;
   RollbackBase *rollback = nullptr;

   try
   {
      CapturePosition(&rollback, err);
   }
   catch (std::bad_alloc)
   {
      ERROR_SET(err, nomem);
   }
   ERROR_CHECK(err);

   while ((frame = GetNextDuration()))
   {
      SkipFrame(err);
      ERROR_CHECK(err);

      r += frame;
   }

   cachedDuration = r;
exit:
   if (rollback) delete rollback;
   return r;
}
