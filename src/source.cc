/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioSource.h>

void
audio::Source::GetStreamInfo(audio::StreamInfo *info, error *err)
{
   info->ContainerHasSlowSeek = this->ContainerHasSlowSeek;

   // If the duration is not known, but it is a local file, or the
   // container has it, then tell the caller it won't take long.
   //
   if (!info->DurationKnown &&
       (!info->FileStreamInfo.IsRemote || !this->ContainerHasSlowSeek))
   {
      info->DurationKnown = true;
   }
}