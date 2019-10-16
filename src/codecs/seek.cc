/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include "seekbase.h"

#include <string.h>
#include <errno.h>

#include <common/time.h>
#include <common/misc.h>

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
   uint64_t seekTableDuration, seekTableOff = 0;

   if (pos >= currentPos && pos <= nextPos)
      return;

   if (seekTable.get() && (seekTable->Lookup(pos, seekTableDuration, seekTableOff, err) || ERROR_FAILED(err)))
   {
      ERROR_CHECK(err);

      SeekToOffset(seekTableOff, seekTableDuration, err);
      ERROR_CHECK(err);
   }
   else if (pos < currentPos)
   {
      SeekToOffset(0, 0, err);
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
   common::Stream *stream = nullptr;
   uint64_t startPos = 0;
   uint64_t startTime = 0;

   try
   {
      CapturePosition(&rollback, err);
   }
   catch (std::bad_alloc)
   {
      ERROR_SET(err, nomem);
   }
   ERROR_CHECK(err);

   stream = rollback->GetStream();
   if (stream)
   {
      startPos = stream->GetPosition(err);
      ERROR_CHECK(err);
      startTime = get_monotonic_time_millis();
   }

   while ((frame = GetNextDuration()))
   {
      SkipFrame(err);
      ERROR_CHECK(err);

      r += frame;

      // If we've been doing this for more than 500 ms without an answer,
      // take the rate of time-per-byte and extrapolate to the entire file.
      //
      if (stream)
      {
         auto end = get_monotonic_time_millis();
         if (end - startTime >= 500)
         {
            common::StreamInfo info;
            stream->GetStreamInfo(&info, err);
            ERROR_CHECK(err);
            auto bytes = stream->GetPosition(err) - startPos;
            ERROR_CHECK(err);
            if (info.FileSizeKnown && bytes)
            {
               auto sz = stream->GetSize(err);
               ERROR_CHECK(err);
               r = MAX(0, sz - startPos) * ((r + 0.0) / bytes);
               break;
            }
         }
      }
   }

   cachedDuration = r;
exit:
   if (rollback) delete rollback;
   return r;
}


//
// XXX this breaks some abstractions, we'll take some guesses about some common
// container formats.
//

bool
audio::IsSlowSeekContainer(common::Stream *stream, error *err)
{
   bool r = false;
   unsigned char buf[4];
   int len = 0;
   auto oldPos = stream->GetPosition(err);
   ERROR_CHECK(err);

   len = stream->Read(buf, sizeof(buf), err);
   ERROR_CHECK(err);

   stream->Seek(SEEK_SET, oldPos, err);
   ERROR_CHECK(err);

   if (len >= 3 && !memcmp(buf, "AMR", 3))
      r = true;
   else if (len >= 2 && buf[0] == 0xff && (buf[1] & 0xf0) == 0xf0)
      r = true;

exit:
   return r;
}