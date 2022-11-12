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

namespace {

#define Unknown audio::Unknown

static const ChannelInfo WindowsMappings[] =
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

#undef Unknown

}

void
audio::ParseWindowsChannelLayout(
   std::vector<ChannelInfo> &out,
   uint32_t mask,
   error *err
)
{
   try
   {
      for (int i=0, j=ARRAY_SIZE(WindowsMappings); i<j; ++i)
      {
         if (mask & (1U << (i)))
            out.push_back(WindowsMappings[i]);
      }
   }
   catch (const std::bad_alloc &)
   {
      ERROR_SET(err, nomem);
   }
exit:;
}

void
audio::ApplyWindowsChannelLayout(Metadata &md, uint32_t mask, error *err)
{
   std::vector<ChannelInfo> out;

   ParseWindowsChannelLayout(out, mask, err);
   ERROR_CHECK(err);

   ApplyChannelLayout(md, out.data(), out.size(), err);
   ERROR_CHECK(err);
exit:;
}

#if defined(__APPLE__)
#include <CoreAudio/CoreAudioTypes.h>
#else
#include <ALACAudioTypes.h>
#define kAudioChannelLayoutTag_MPEG_3_0_B kALACChannelLayoutTag_MPEG_3_0_B
#define kAudioChannelLayoutTag_MPEG_4_0_B kALACChannelLayoutTag_MPEG_4_0_B
#define kAudioChannelLayoutTag_MPEG_5_0_D kALACChannelLayoutTag_MPEG_5_0_D
#define kAudioChannelLayoutTag_MPEG_5_1_D kALACChannelLayoutTag_MPEG_5_1_D
#define kAudioChannelLayoutTag_AAC_6_1    kALACChannelLayoutTag_AAC_6_1
#define kAudioChannelLayoutTag_MPEG_7_1_B kALACChannelLayoutTag_MPEG_7_1_B

enum
{
   kAudioChannelLayoutTag_MPEG_3_0_A = (113U<<16) | 3,
   kAudioChannelLayoutTag_MPEG_4_0_A = (115U<<16) | 4,
   kAudioChannelLayoutTag_MPEG_5_0_A = (117U<<16) | 5,
   kAudioChannelLayoutTag_MPEG_5_0_B = (118U<<16) | 5,
   kAudioChannelLayoutTag_MPEG_5_0_C = (119U<<16) | 5,
   kAudioChannelLayoutTag_MPEG_5_1_A = (121U<<16) | 6,
   kAudioChannelLayoutTag_MPEG_5_1_B = (122U<<16) | 6,
   kAudioChannelLayoutTag_MPEG_5_1_C = (123U<<16) | 6,
   kAudioChannelLayoutTag_MPEG_6_1_A = (125U<<16) | 7,
   kAudioChannelLayoutTag_MPEG_7_1_A = (126U<<16) | 8,
};
#endif

namespace {

#define DECLARE_VALUE(FORMAT, ...) \
static const ChannelInfo FORMAT##_Values[] = __VA_ARGS__

// These are from ALAC:
//
DECLARE_VALUE(MPEG_3_0_B, { FrontCenter, FrontLeft, FrontRight });
DECLARE_VALUE(MPEG_4_0_B, { FrontCenter, FrontLeft, FrontRight, RearCenter });
DECLARE_VALUE(MPEG_5_0_D, { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight });
DECLARE_VALUE(MPEG_5_1_D, { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight,  LFE });
DECLARE_VALUE(AAC_6_1,    { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight,  RearCenter, LFE });
DECLARE_VALUE(MPEG_7_1_B, { FrontCenter, SideLeft,  SideRight,  FrontLeft, FrontRight, RearLeft,   RearRight, LFE });

// Other interesting ones:
//
DECLARE_VALUE(MPEG_3_0_A, { FrontLeft, FrontRight,  FrontCenter });
DECLARE_VALUE(MPEG_4_0_A, { FrontLeft, FrontRight,  FrontCenter, RearCenter });
DECLARE_VALUE(MPEG_5_0_A, { FrontLeft, FrontRight,  FrontCenter, RearLeft,  RearRight });
DECLARE_VALUE(MPEG_5_0_B, { FrontLeft, FrontRight,  RearLeft,    RearRight, FrontCenter });
DECLARE_VALUE(MPEG_5_0_C, { FrontLeft, FrontCenter, FrontRight,  RearLeft,  RearRight });
DECLARE_VALUE(MPEG_5_1_A, { FrontLeft, FrontRight,  FrontCenter, LFE,       RearLeft,    RearRight });
DECLARE_VALUE(MPEG_5_1_B, { FrontLeft, FrontRight,  RearLeft,    RearRight, FrontCenter, LFE });
DECLARE_VALUE(MPEG_5_1_C, { FrontLeft, FrontCenter, FrontRight,  RearLeft,  RearRight,   LFE });
DECLARE_VALUE(MPEG_6_1_A, { FrontLeft, FrontRight,  FrontCenter, LFE,       RearLeft,    RearRight, RearCenter });
DECLARE_VALUE(MPEG_7_1_A, { FrontLeft, FrontRight,  FrontCenter, LFE,       RearLeft,    RearRight, SideLeft,  SideRight });

#undef DECLARE_VALUE

struct AppleChannelLayoutMapping
{
   uint32_t LayoutTag;
   const ChannelInfo *Values;
   int N;
};

static AppleChannelLayoutMapping AppleMappings[] =
{
#define REF_VALUE(NAME) {kAudioChannelLayoutTag_##NAME, NAME##_Values, ARRAY_SIZE(NAME##_Values)}
   REF_VALUE(MPEG_3_0_B),
   REF_VALUE(MPEG_4_0_B),
   REF_VALUE(MPEG_5_0_D),
   REF_VALUE(MPEG_5_1_D),
   REF_VALUE(AAC_6_1),
   REF_VALUE(MPEG_7_1_B),

   REF_VALUE(MPEG_3_0_A),
   REF_VALUE(MPEG_4_0_A),
   REF_VALUE(MPEG_5_0_A),
   REF_VALUE(MPEG_5_0_B),
   REF_VALUE(MPEG_5_0_C),
   REF_VALUE(MPEG_5_1_A),
   REF_VALUE(MPEG_5_1_B),
   REF_VALUE(MPEG_5_1_C),
   REF_VALUE(MPEG_6_1_A),
   REF_VALUE(MPEG_7_1_A),
#undef REF_VALUE
};

} // namespace

void
audio::ApplyAppleChannelLayout(Metadata &md, uint32_t tag, error *err)
{
   const ChannelInfo *p = nullptr;
   int n = 0;

   for (int i=0; i<ARRAY_SIZE(AppleMappings); ++i)
   {
      auto &mapping = AppleMappings[i];
      if (mapping.LayoutTag == tag)
      {
         p = mapping.Values;
         n = mapping.N;
         break;
      }
   }

   if (p && n)
   {
      ApplyChannelLayout(md, p, n, err);
      ERROR_CHECK(err);
   }
exit:;
}
