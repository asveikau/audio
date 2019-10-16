/*
 Copyright (C) 2017 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef seekbase_h_
#define seekbase_h_

#include "rollback.h"
#include <common/c++/stream.h>
#include <memory>

namespace audio {

struct SeekTable;

class SeekBase
{
   uint64_t cachedDuration;
   std::shared_ptr<SeekTable> seekTable;
protected:
   virtual uint64_t GetPosition(void) = 0;
   virtual uint64_t GetNextDuration(void) = 0;
   virtual void SeekToOffset(uint64_t off, uint64_t time, error *err) = 0;
   virtual void SkipFrame(error *err) = 0;
   virtual void CapturePosition(RollbackBase **rollback, error *err) = 0;
public:
   SeekBase(uint64_t duration = 0);
   void Seek(uint64_t pos, error *err);
   uint64_t GetDuration(error *err);
   bool GetDurationKnown(void) const { return cachedDuration != 0; }
   void SetCachedDuration(uint64_t duration) { cachedDuration = duration; }
   void SetSeekTable(const std::shared_ptr<SeekTable> &seekTable) { this->seekTable = seekTable; }
};

bool
IsSlowSeekContainer(common::Stream *str, error *err);

} // end namespace

#endif
