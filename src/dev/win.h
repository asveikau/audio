/*
 Copyright (C) 2017 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#pragma once

namespace audio { namespace windows {

static inline void
MetadataToWaveFormatEx(const Metadata &md, WAVEFORMATEX *wfe)
{
   wfe->wFormatTag = WAVE_FORMAT_PCM;
   wfe->nChannels = md.Channels;
   wfe->nSamplesPerSec = md.SampleRate;
   wfe->wBitsPerSample = GetBitsPerSample(md.Format);
   wfe->nBlockAlign = wfe->nChannels * wfe->wBitsPerSample / 8;
   wfe->nAvgBytesPerSec = wfe->nSamplesPerSec * wfe->nBlockAlign;
   wfe->cbSize = 0;
}

} } // end namespace
