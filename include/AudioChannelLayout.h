/*
 Copyright (C) 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef audio_channel_layout_h_
#define audio_channel_layout_h_

#include <stddef.h>

#include <common/error.h>

#include "AudioSource.h"

#include <stdint.h>

#include <vector>
#include <memory>

namespace audio {

void
ParseWindowsChannelLayout(
   std::vector<ChannelInfo> &out,
   uint32_t mask,
   error *err
);

void
GetCommonWavChannelLayout(
   int numChannels,
   const ChannelInfo *&channelInfo,
   int &n
);

void
GetCommonOggChannelLayout(
   int numChannels,
   const ChannelInfo *&channelInfo,
   int &n
);

void
ApplyChannelLayout(
   Metadata &md,
   const ChannelInfo *info,
   int n,
   error *err
);

void
ApplyChannelLayout(
   Metadata &md,
   void (*fn)(int, const ChannelInfo*&, int &),
   error *err
);

} // namespace

#endif

