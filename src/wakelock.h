/*
 Copyright (C) 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef audio_wakelock_h_
#define audio_wakelock_h_

#include <common/c++/refcount.h>
#include <common/error.h>

namespace audio
{

void
CreateWakeLock(
   common::RefCountable **p,
   error *err
);

} // end namespace

#endif