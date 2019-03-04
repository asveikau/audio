/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef rollback_h_
#define rollback_h_

#include <string.h>

namespace audio
{

template<typename T>
class RollbackItem
{
   T& ref, oldValue;
public:
   RollbackItem(T& ref_) : ref(ref_), oldValue(ref_) {}
   ~RollbackItem() { ref = oldValue; }
};

template<typename... Args>
class RollbackImpl;

template<typename... Args>
class RollbackIter
{
};

template<typename First, typename... Args>
class RollbackIter<First, Args...> : public RollbackImpl<Args...>
{
   RollbackItem<First> first;  
protected:
   RollbackIter(First &first_, Args&... a) : RollbackImpl<Args...>(a...), first(first_) {}
};

template<typename... Args>
class RollbackImpl : public RollbackIter<Args...>
{
protected:
   RollbackImpl(Args&... a) : RollbackIter<Args...>(a...) {};
};

struct RollbackBase { virtual ~RollbackBase() {} };

template<typename... Args>
class Rollback : public RollbackBase, public RollbackImpl<Args...>
{
public:
   Rollback(Args&... a) : RollbackImpl<Args...>(a...) {};
};

template<typename... Args>
static inline RollbackBase*
CreateRollback(Args&... a)
{
   return new Rollback<Args...>(a...);
}

template<typename... Args>
class RollbackWithCursorPos : public Rollback<Args...>
{
   common::Pointer<common::Stream> stream;
   error *err;
   uint64_t oldPos;
   bool hasOldPos;
public:
   RollbackWithCursorPos(common::Stream *stream_, error *err_, Args&... a)
      : Rollback<Args...>(a...), stream(stream_), err(err_), oldPos(0), hasOldPos(false)
   {
      if (!ERROR_FAILED(err))
         oldPos = stream->GetPosition(err);
      hasOldPos = !ERROR_FAILED(err);
   }

   ~RollbackWithCursorPos()
   {
      if (hasOldPos)
      {
         error innerErr;
         if (ERROR_FAILED(err))
            err = &innerErr;

         stream->Seek(oldPos, SEEK_SET, err);
      }
   }
}; 

template<typename... Args>
static inline RollbackBase*
CreateRollbackWithCursorPos(common::Stream *stream, error *err, Args&... a)
{
   return new RollbackWithCursorPos<Args...>(stream, err, a...);
}

} // end namespace

#endif
