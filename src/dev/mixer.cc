/*
 Copyright (C) 2021 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioDevice.h>

audio::MuteState
audio::Mixer::GetMuteState(int idx, error *err)
{
   return MuteState::None;
}

void
audio::Mixer::SetMute(int idx, bool on, error *err)
{
}

bool
audio::Mixer::CanMute(int idx)
{
   error err;
   return (GetMuteState(idx, &err) & MuteState::CanMute) ? true : false;
}

bool
audio::Mixer::IsMuted(int idx, error *err)
{
   return (GetMuteState(idx, err) & MuteState::Muted) ? true : false;
}

audio::MuteState
audio::SoftMuteMixer::GetMuteState(int idx, error *err)
{
   MuteState r = MuteState::CanMute | MuteState::SoftMute;

   if (oldValues.find(idx) != oldValues.end())
      r |= MuteState::Muted;

   return r;
}

void
audio::SoftMuteMixer::SetMute(int idx, bool on, error *err)
{
   auto existing = oldValues.find(idx);

   // Already muted?
   //
   if (existing != oldValues.end())
   {
      if (!on)
      {
         SetValue(idx, &existing->second, 1, err);
         ERROR_CHECK(err);
         oldValues.erase(existing);
      }
   }
   else
   {
      if (on)
      {
         float val = 0.0f;
         float mute = 0.0f;

         GetValue(idx, &val, 1, err);
         ERROR_CHECK(err);

         try
         {
            oldValues[idx] = val;
         }
         catch (const std::bad_alloc &)
         {
            ERROR_SET(err, nomem);
         }

         SetValue(idx, &mute, 1, err);
         if (ERROR_FAILED(err))
         {
            oldValues.erase(idx);
            ERROR_CHECK(err);
         }
      }
   }
exit:;
}