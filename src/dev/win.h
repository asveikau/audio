/*
 Copyright (C) 2017, 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#pragma once

#include <AudioChannelLayout.h>

namespace audio { namespace windows {

static inline void
MetadataToWaveFormatEx(const Metadata &md, WAVEFORMATEXTENSIBLE *wfe)
{
   bool needEx = false;

   auto fmt = &wfe->Format;
   fmt->nChannels = md.Channels;
   fmt->nSamplesPerSec = md.SampleRate;
   fmt->wBitsPerSample = GetBitsPerSample(md.Format);
   fmt->nBlockAlign = fmt->nChannels * fmt->wBitsPerSample / 8;
   fmt->nAvgBytesPerSec = fmt->nSamplesPerSec * fmt->nBlockAlign;

   switch (fmt->wBitsPerSample)
   {
   case 8:
   case 16:
      needEx = false;
      break;
   default:
      needEx = true;
      break;
   }

   if (fmt->nChannels > 2)
      needEx = true;

   if (!needEx)
   {
      fmt->wFormatTag = WAVE_FORMAT_PCM;
      fmt->cbSize = 0;

      // Zero out the end of the struct.
      //
      auto end = (char*)fmt + sizeof(*fmt);
      memset(end, 0, sizeof(*wfe) - (end - (char*)wfe));
   }
   else
   {
      fmt->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
      fmt->cbSize = sizeof(*wfe) - sizeof(wfe->Format);

      switch (md.Format)
      {
      case Pcm24Pad:
         wfe->Samples.wValidBitsPerSample = 24;
         break;
      default:
         wfe->Samples.wValidBitsPerSample = fmt->wBitsPerSample;
      }

      if (md.ChannelMap.get() && md.ChannelMap->size())
      {
         wfe->dwChannelMask = 0;

         for (auto ch : *md.ChannelMap.get())
         {
            wfe->dwChannelMask |= ChannelInfoToWindowsChannelBit(ch);
         }

         goto skipDefaults;
      }

      // XXX This is probably not right
      switch (fmt->nChannels)
      {
      case 1:
         wfe->dwChannelMask = SPEAKER_FRONT_CENTER;
         break;
      case 2:
         wfe->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
         break;
     case 3:
         wfe->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY;
         break;
      case 4:
         wfe->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
         break;
      case 5:
         wfe->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_LOW_FREQUENCY;
         break;
      case 6:
         wfe->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
         break;
      case 7:
         wfe->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_BACK_CENTER;
         break;
      case 8:
         wfe->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
         break;
      default:
         wfe->dwChannelMask = 0;
      }

skipDefaults:
      switch (md.Format)
      {
      case PcmFloat:
         wfe->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
         break;
      default:
         wfe->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
      }
   }
}

static inline int
GetChannelMap(DWORD dwChannelMask, ChannelInfo *info, int n, error *err)
{
   int r = 0;
   for (int i = 0; i<sizeof(dwChannelMask)*8; ++i)
   {
      if (((1U << i) & dwChannelMask))
      {
         if (!n)
            ERROR_SET(err, unknown, "Not enough buffer space");
         *info++ = WindowsChannelBitToChannelInfo(i);
         --n;
         ++r;
      }
   }
exit:
   if (ERROR_FAILED(err))
      r = 0;
   return r;
}


} } // end namespace
