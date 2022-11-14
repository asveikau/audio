/*
 Copyright (C) 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include "AudioChannelLayout.h"
#include "AudioTransform.h"

#include <common/misc.h>

#include <unordered_map>

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
   kAudioChannelLayoutTag_Quadraphonic = (108U<<16) | 4,
   kAudioChannelLayoutTag_Pentagonal   = (109U<<16) | 5,
   kAudioChannelLayoutTag_MPEG_3_0_A   = (113U<<16) | 3,
   kAudioChannelLayoutTag_MPEG_4_0_A   = (115U<<16) | 4,
   kAudioChannelLayoutTag_MPEG_5_0_A   = (117U<<16) | 5,
   kAudioChannelLayoutTag_MPEG_5_0_B   = (118U<<16) | 5,
   kAudioChannelLayoutTag_MPEG_5_0_C   = (119U<<16) | 5,
   kAudioChannelLayoutTag_MPEG_5_1_A   = (121U<<16) | 6,
   kAudioChannelLayoutTag_MPEG_5_1_B   = (122U<<16) | 6,
   kAudioChannelLayoutTag_MPEG_5_1_C   = (123U<<16) | 6,
   kAudioChannelLayoutTag_MPEG_6_1_A   = (125U<<16) | 7,
   kAudioChannelLayoutTag_MPEG_7_1_A   = (126U<<16) | 8,
};
#endif

namespace {

#define DECLARE_VALUE(FORMAT, ...) \
static const ChannelInfo FORMAT##_Values[] = __VA_ARGS__

//
// These are from ALAC:
//

DECLARE_VALUE(MPEG_3_0_B, { FrontCenter, FrontLeft, FrontRight });
DECLARE_VALUE(MPEG_4_0_B, { FrontCenter, FrontLeft, FrontRight, RearCenter });
DECLARE_VALUE(MPEG_5_0_D, { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight });
DECLARE_VALUE(MPEG_5_1_D, { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight,  LFE });
DECLARE_VALUE(AAC_6_1,    { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight,  RearCenter, LFE });
DECLARE_VALUE(MPEG_7_1_B, { FrontCenter, SideLeft,  SideRight,  FrontLeft, FrontRight, RearLeft,   RearRight, LFE });

//
// Other interesting ones:
//

DECLARE_VALUE(Quadraphonic, { FrontLeft, FrontRight, RearLeft, RearRight });
// Same as mpeg5.0b
DECLARE_VALUE(Pentagonal,   { FrontLeft, FrontRight, RearLeft, RearRight, FrontCenter });

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

   REF_VALUE(Quadraphonic),
   REF_VALUE(Pentagonal),

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

namespace {

struct ChannelMapTransform : public Transform
{
   const void *ZeroBuf;
   int Bps;

   struct Op
   {
      enum
      {
         Zero,
         Move,
      } Action;
      int SrcIndex;
      int DstIndex;
      int Length;
      int ScratchOffset;

      Op(int idx) :
         Action(Zero),
         SrcIndex(idx),
         DstIndex(idx),
         Length(1),
         ScratchOffset(-1)
      {}
   };

   std::vector<Op> ops;
   std::vector<unsigned char> scratchBuf;

   int nsc, ntc;

   virtual ~ChannelMapTransform()
   {
   }

   void
   Initialize(
      Format format,
      const ChannelInfo *sc, int nsc,
      const ChannelInfo *tc, int ntc,
      error *err
   )
   {
      this->nsc = nsc;
      this->ntc = ntc;

      Bps = GetBitsPerSample(format)/8;
      switch (format)
      {
      case PcmFloat:
         {
            static const float zero = 0.0f;
            ZeroBuf = &zero;
         }
         break;
      default:
         ZeroBuf = nullptr;
      }

      try
      {
         std::unordered_map<int /*really ChannelInfo*/, int> srcIndex;
         int scratchLen = 0;
         for (int i=0; i<nsc; ++i)
            srcIndex[(int)sc[i]] = i;
         for (int i=0; i<ntc; ++i)
         {
            Op op(i);

            auto key = tc[i];
            auto srcKey = srcIndex.find((int)key);
            if (srcKey != srcIndex.end())
            {
               auto j = srcKey->second;
               if (i == j && nsc >= ntc)
                  continue;
               op.Action = Op::Move;
               op.SrcIndex = j;
               op.ScratchOffset = scratchLen++;
               for (auto &oldOp : ops)
               {
                  if (oldOp.Action == Op::Move &&
                      oldOp.SrcIndex + oldOp.Length == op.SrcIndex &&
                      oldOp.DstIndex + oldOp.Length == op.DstIndex)
                  {
                     oldOp.Length += op.Length;
                     continue;
                  }
               }
            }
            if (op.Action == Op::Zero &&
                ops.size() &&
                ops[ops.size()-1].Action == Op::Zero &&
                ops[ops.size()-1].DstIndex + ops[ops.size()-1].Length == i)
            {
               ops[ops.size()-1].Length++;
               continue;
            }
            ops.push_back(op);
         }
         if (nsc >= ntc)
            scratchBuf.resize(scratchLen * Bps);
      }
      catch (const std::bad_alloc &)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   void
   TransformAudioPacket(void *&buf, size_t &len, error *err)
   {
      auto src = (const unsigned char*)buf;
      size_t srclen = len;
      unsigned char *dst, *dstStart;
      bool grow = false;

      if (ntc > nsc)
      {
         // Need to grow buffer;
         //
         size_t dstlen = len / nsc * ntc * Bps;
         try
         {
            scratchBuf.resize(dstlen);
         }
         catch (const std::bad_alloc &)
         {
            ERROR_SET(err, nomem);
         }
         dst = scratchBuf.data();
         grow = true;
      }
      else
      {
         dst = (unsigned char*)buf;
      }
      dstStart = dst;

      while (srclen)
      {
         if (!grow)
         {
            for (auto &op : ops)
            {
               if (op.Action == Op::Move)
                  memcpy(scratchBuf.data() + op.ScratchOffset * Bps, src + op.SrcIndex*Bps, op.Length * Bps);
            }
         }
         for (auto &op : ops)
         {
            switch (op.Action)
            {
            case Op::Zero:
               if (ZeroBuf)
               {
                  for (int i=0; i<op.Length; ++i)
                  {
                     memcpy(dst + (op.DstIndex + i)*Bps, ZeroBuf, Bps);
                  }
               }
               else
                  memset(dst + op.DstIndex * Bps, 0, op.Length * Bps);
               break;
            case Op::Move:
               memcpy(
                  dst + op.DstIndex * Bps,
                  grow
                      ? src + op.SrcIndex * Bps
                      : scratchBuf.data() + op.ScratchOffset * Bps,
                  op.Length * Bps
               );
               break;
            }
         }
         src += nsc * Bps;
         srclen -= nsc * Bps;
         dst += ntc * Bps;
      }

      buf = dstStart;
      len = dst - dstStart;
   exit:;
   }
};

} // end namespace

Transform*
audio::CreateChannelMapTransform(
   Format format,
   const ChannelInfo *sourceChannels,
   int nSourceChannels,
   const ChannelInfo *targetChannels,
   int nTargetChannels,
   error *err
)
{
   ChannelMapTransform *r = nullptr;
   if (!r)
      ERROR_SET(err, nomem);
   r->Initialize(format, sourceChannels, nSourceChannels, targetChannels, nTargetChannels, err);
   if (ERROR_FAILED(err))
   {
      delete r;
      r = nullptr;
   }
exit:
   return r;
}
