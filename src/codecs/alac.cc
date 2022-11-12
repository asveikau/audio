/*
 Copyright (C) 2019, 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <MicroCodec.h>
#include <AudioChannelLayout.h>
#include <common/c++/new.h>
#include <common/misc.h>

#define PRAGMA_STRUCT_PACKPUSH 1
#include <ALACDecoder.h>
#include <ALACBitUtilities.h>

namespace {

uint32_t
read32(const uint32_t *ptr)
{
   const unsigned char *p = (const unsigned char *)ptr;
   return *p |
          (((uint32_t)p[1]) << 8) |
          (((uint32_t)p[2]) << 16) |
          (((uint32_t)p[3]) << 24);
}

// XXX this is missing from the library header
//
#pragma pack(1)
typedef struct ALACChannelLayoutInfo
{
        uint32_t        channelLayoutInfoSize;
        uint32_t        channelLayoutInfoID;
        uint32_t        versionFlags;
        uint32_t        channelLayoutTag;
        uint32_t        reserved1;
        uint32_t        reserved2;
} ALACChannelLayoutInfo;
#pragma pack()

struct AlacCodec : public audio::MicroCodec
{
   ALACDecoder decoder;
   uint32_t channelLayout;
   char desc[128];

   AlacCodec() : channelLayout(0)
   {
      desc[0] = 0;
   }

   void
   Initialize(
      const void *config,
      int nbytes,
      error *err
   )
   {
      int32_t status = 0;

      status = decoder.Init((void*)config, nbytes);
      if (status)
         ERROR_SET(err, alac, status);

      // This is documented in the Apple repo, but ffmpeg does not write it.
      // Supposedly the tags are theroetically more broad than this.
      //
      if (nbytes > sizeof(ALACSpecificConfig) &&
          nbytes - sizeof(ALACSpecificConfig) >= sizeof(ALACChannelLayoutInfo))
      {
         auto channelHeader =
            (const ALACChannelLayoutInfo*)((char*)config + sizeof(ALACSpecificConfig));
         if (read32(&channelHeader->channelLayoutInfoSize) >= sizeof(*channelHeader) &&
             !memcmp(&channelHeader->channelLayoutInfoID, "chan", 4))
         {
            channelLayout = read32(&channelHeader->channelLayoutTag);
         }
      }

      // XXX if missing, assume it was there based on channel count.
      //
      if (!channelLayout)
      {
         switch (decoder.mConfig.numChannels)
         {
         case 3:
            channelLayout = kALACChannelLayoutTag_MPEG_3_0_B;
            break;
         case 4:
            channelLayout = kALACChannelLayoutTag_MPEG_4_0_B;
            break;
         case 5:
            channelLayout = kALACChannelLayoutTag_MPEG_5_0_D;
            break;
         case 6:
            channelLayout = kALACChannelLayoutTag_MPEG_5_1_D;
            break;
         case 7:
            channelLayout = kALACChannelLayoutTag_AAC_6_1;
            break;
         case 8:
            channelLayout = kALACChannelLayoutTag_MPEG_7_1_B;
            break;
         }
      }

      // XXX
      switch (decoder.mConfig.bitDepth)
      {
      case 16:
      case 24:
         break;
      default:
         ERROR_SET(err, unknown, "Untested bitdepth");
      }
   exit:;
   }

   const char *
   Describe()
   {
      snprintf(desc, sizeof(desc), "[alac] bps=%d", decoder.mConfig.bitDepth);
      return desc;
   }

   void
   GetMetadata(audio::Metadata *md, error *err)
   {
      md->SampleRate = decoder.mConfig.sampleRate;
      md->Channels = decoder.mConfig.numChannels;

      switch (decoder.mConfig.bitDepth)
      {
      case 16:
         md->Format = audio::PcmShort;
         break;
      case 24:
         md->Format = audio::Pcm24;
         break;
      default:
         ERROR_SET(err, unknown, "Untested bitdepth");
      }

      md->SamplesPerFrame = decoder.mConfig.frameLength;

      if (channelLayout)
      {
         audio::ApplyAppleChannelLayout(*md, channelLayout, err);
         ERROR_CHECK(err);
      }

   exit:;
   }

   int
   Decode(
      const void *samples,
      int samplesNBytes,
      void *outputBuffer,
      int outputBufferNBytes,
      error *err
   )
   {
      int32_t status = 0;
      uint32_t outLen = 0;
      int r = 0;
      BitBuffer bb;

      int bps = decoder.mConfig.bitDepth / 8;
      int channels = decoder.mConfig.numChannels;

      BitBufferInit(&bb, (unsigned char*)samples, samplesNBytes);

      status = decoder.Decode(
         &bb,
         (unsigned char*)outputBuffer,
         outputBufferNBytes/bps/channels,
         channels,
         &outLen
      );
      if (status)
         ERROR_SET(err, alac, status);
      r = outLen * bps * channels;
   exit:
      return r;
   }

   void
   error_set_alac(error *err, int32_t status)
   {
      switch (status)
      {
      case kALAC_UnimplementedError:
         error_set_errno(err, ENOSYS);
         break;
      case kALAC_FileNotFoundError:
         error_set_errno(err, ENOENT);
         break;
      case kALAC_ParamError:
         error_set_errno(err, EINVAL);
         break;
      case kALAC_MemFullError:
         error_set_nomem(err);
         break;
      default:
         error_set_unknown(err, "ALAC error");
      }
   }
};

} // end namespace

void
audio::CreateAlacCodec(
   MicroCodec **out,
   error *err
)
{
   common::Pointer<AlacCodec> r;
   New(r, err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err))
      r = nullptr;
   *out = r.Detach();
}
