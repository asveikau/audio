/*
 Copyright (C) 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include "AudioChannelLayout.h"

#include <common/misc.h>

using namespace audio;

#define CASE(NCHANNELS,...)                 \
   case NCHANNELS:                          \
   {                                        \
      static const ChannelInfo arr[] = __VA_ARGS__; \
      channelInfo = arr;                    \
      n = ARRAY_SIZE(arr);                  \
   }                                        \
   break

// flac

void
audio::GetCommonWavChannelLayout(
   int numChannels,
   const ChannelInfo *&channelInfo,
   int &n
)
{
   switch (numChannels)
   {
      CASE(3, {FrontLeft, FrontRight, FrontCenter});
      CASE(4, {FrontLeft, FrontRight, RearLeft, RearRight});
      CASE(5, {FrontLeft, FrontRight, FrontCenter, RearLeft, RearRight});
      CASE(6, {FrontLeft, FrontRight, FrontCenter, LFE, RearLeft, RearRight});
      CASE(7, {FrontLeft, FrontRight, FrontCenter, LFE, RearCenter, SideLeft, SideRight});
      CASE(8, {FrontLeft, FrontRight, FrontCenter, LFE, RearCenter, SideLeft, SideRight, RearLeft, RearRight});
   default:
      n = 0;
   }
}

// vorbis, opus

void
audio::GetCommonOggChannelLayout(
   int numChannels,
   const ChannelInfo *&channelInfo,
   int &n
)
{
   if (numChannels <= 4)
   {
      GetCommonWavChannelLayout(numChannels, channelInfo, n);
      return;
   }

   switch (numChannels)
   {
      CASE(5, {FrontLeft, FrontCenter, FrontRight, RearLeft, RearRight});
      CASE(6, {FrontLeft, FrontCenter, FrontRight, RearLeft, RearRight, LFE});
      CASE(7, {FrontLeft, FrontCenter, FrontRight, SideLeft, SideRight, RearCenter, LFE});
      CASE(8, {FrontLeft, FrontCenter, FrontRight, SideLeft, SideRight, RearLeft, RearRight, LFE});
   default:
      n = 0;
   }
}

void
audio::ApplyChannelLayout(
   Metadata &md,
   const ChannelInfo *info,
   int n,
   error *err
)
{
   if (n)
   {
      try
      {
         md.ChannelMap = std::make_shared<std::vector<ChannelInfo>>();
         md.ChannelMap->insert(md.ChannelMap->end(), info, info+n);
      }
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }
   }
   else
   {
      md.ChannelMap.reset();
   }
exit:;
}

void
audio::ApplyChannelLayout(
   Metadata &md,
   void (*fn)(int, const ChannelInfo*&, int &),
   error *err
)
{
   if (md.Channels > 2)
   {
      const ChannelInfo *info = nullptr;
      int n = 0;

      fn(md.Channels, info, n);
      ApplyChannelLayout(md, info, n, err);
      ERROR_CHECK(err);
      goto exit;
   }
   md.ChannelMap.reset();
exit:;
}

void
audio::ParseWindowsChannelLayout(
   std::vector<ChannelInfo> &out,
   uint32_t mask,
   error *err
)
{
   static const ChannelInfo mappings[] =
   {
      FrontLeft,
      FrontRight,
      FrontCenter,
      LFE,
      RearLeft,
      RearRight,
      Unknown, // FrontLeftOfCenter
      Unknown, // FrontRightOfCenter
      RearCenter,
      SideLeft,
      SideRight,
      Unknown, // TopCenter
      Unknown, // TopFrontLeft,
      Unknown, // TopFrontCenter,
      Unknown, // TopFrontRight,
      Unknown, // TopRearLeft,
      Unknown, // TopRearCenter,
      Unknown, // TopRearRight,
   };
   try
   {
      for (int i=0, j=ARRAY_SIZE(mappings); i<j; ++i)
      {
         if (mask & (1U << (i)))
            out.push_back(mappings[i]);
      }
   }
   catch (const std::bad_alloc &)
   {
      ERROR_SET(err, nomem);
   }
exit:;
}
