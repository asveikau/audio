/*
 Copyright (C) 2017, 2018, 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#import <AudioToolbox/AudioToolbox.h>

#include <AudioCodec.h>
#include <AudioChannelLayout.h>

#include "seekbase.h"

#include <common/c++/new.h>
#include <common/misc.h>

using namespace common;
using namespace audio;

namespace {

class CoreAudioSource : public Source
{
   Pointer<Stream> stream;
   AudioFileID id;
   ExtAudioFileRef extFile;
   Format format;
   int sampleRate;
   int channels;
   int blockAlign;
   SInt64 tellFrameCorrection;
   UInt64 cachedDuration;
   char description[128];

public:

   CoreAudioSource()
      : id(nullptr),
        extFile(nullptr),
        sampleRate(0),
        channels(0),
        blockAlign(0),
        tellFrameCorrection(0),
        cachedDuration(0)
   {}

   ~CoreAudioSource()
   {
      if (extFile)
         ExtAudioFileDispose(extFile);
      if (id)
         AudioFileClose(id);
   }

   const char *Describe(void)
   {
      OSStatus status = 0;
      AudioStreamBasicDescription desc = {0};
      UInt32 sz = sizeof(desc);
      static const char tag[] = "[extaudiofile]";
      char fmtBuffer[5];

      status = ExtAudioFileGetProperty(
         extFile,
         kExtAudioFileProperty_FileDataFormat,
         &sz,
         &desc
      );
      if (status)
         return tag;

      snprintf(
         description,
         sizeof(description),
         "%s %s",
         tag,
         DescribeFormat(desc.mFormatID, fmtBuffer)
      ); 

      return description;
   }

   void Initialize(Stream *file, CodecArgs &params, error *err)
   {
      OSStatus status = 0;
      AudioStreamBasicDescription targetFormat;
      AudioStreamBasicDescription nativeFormat;
      UInt32 nativeFormatSize = sizeof(nativeFormat);

      this->stream = file;
      this->cachedDuration = params.Duration;

      // I/O callbacks can't handle non-zero start offset.
      //
      file->Seek(0, SEEK_SET, err);
      ERROR_CHECK(err);

      ContainerHasSlowSeek = IsSlowSeekContainer(file, err);
      ERROR_CHECK(err);

      status = AudioFileOpenWithCallbacks(
         this,
         ReadStatic,
         nullptr,
         GetSizeStatic,
         nullptr,
         0,
         &id
      );
      if (status) ERROR_SET(err, osstatus, status);

      status = ExtAudioFileWrapAudioFileID(id, false, &extFile);
      if (status) ERROR_SET(err, osstatus, status);

      status = ExtAudioFileGetProperty(
         extFile,
         kExtAudioFileProperty_FileDataFormat,
         &nativeFormatSize,
         &nativeFormat
      );
      if (status) ERROR_SET(err, osstatus, status);

      targetFormat.mFormatID = kAudioFormatLinearPCM;
      targetFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger;
      if (nativeFormat.mBitsPerChannel <= 16)
      {
         targetFormat.mBitsPerChannel = 16;
         format = PcmShort;
      }
      else
      {
         targetFormat.mBitsPerChannel = 24;
         format = Pcm24;
      }

      targetFormat.mSampleRate = nativeFormat.mSampleRate;
      targetFormat.mChannelsPerFrame = nativeFormat.mChannelsPerFrame;

      targetFormat.mBytesPerFrame =
         targetFormat.mChannelsPerFrame * targetFormat.mBitsPerChannel / 8;
      targetFormat.mBytesPerPacket = targetFormat.mBytesPerFrame;
      targetFormat.mFramesPerPacket = 1;

      sampleRate = targetFormat.mSampleRate;
      channels = targetFormat.mChannelsPerFrame;
      blockAlign = targetFormat.mBytesPerPacket;

      status = ExtAudioFileSetProperty(
         extFile,
         kExtAudioFileProperty_ClientDataFormat,
         sizeof(targetFormat),
         &targetFormat
      );
      if (status) ERROR_SET(err, osstatus, status);

      status = ExtAudioFileTell(extFile, &tellFrameCorrection); 
      if (status) ERROR_SET(err, osstatus, status);
      tellFrameCorrection *= -1;

   exit:;
   }

   void GetMetadata(Metadata *res, error *err)
   {
      AudioChannelLayout *layout = nullptr;

      res->Format = format;
      res->SampleRate = sampleRate;
      res->Channels = channels;
      res->SamplesPerFrame = 0;

      if (channels > 2)
      {
         OSStatus status = 0;
         UInt32 len = 0;

         len = sizeof(*layout);
         layout = (AudioChannelLayout*)malloc(len);
         if (!layout)
            ERROR_SET(err, nomem);

         status = ExtAudioFileGetProperty(
            extFile,
            kExtAudioFileProperty_FileChannelLayout,
            &len,
            layout
         );
         if (status) ERROR_SET(err, osstatus, status);

         if (len > sizeof(*layout))
         {
            void *ptr = realloc(layout, len);
            if (!ptr)
               ERROR_SET(err, nomem);
            layout = (AudioChannelLayout*)ptr;

            status = ExtAudioFileGetProperty(
               extFile,
               kExtAudioFileProperty_FileChannelLayout,
               &len,
               layout
            );
            if (status) ERROR_SET(err, osstatus, status);
         }

         if (layout->mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelBitmap)
         {
            try
            {
               res->ChannelMap = std::make_shared<std::vector<ChannelInfo>>();
               ParseWindowsChannelLayout(*res->ChannelMap, layout->mChannelBitmap, err);
               ERROR_CHECK(err);
            }
            catch (const std::bad_alloc &)
            {
               ERROR_SET(err, nomem);
            }
         }
         else if (layout->mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelDescriptions)
         {
            std::vector<ChannelInfo> mapping;

            for (auto i=0; i<layout->mNumberChannelDescriptions; ++i)
            {
               auto label = layout->mChannelDescriptions[i].mChannelLabel;
               static const ChannelInfo earlyMappings[] =
               {
                  FrontLeft,
                  FrontRight,
                  FrontCenter,
                  LFE,
                  RearLeft,
                  RearRight,
                  Unknown, // LeftCenter,
                  Unknown, // RightCenter
                  RearCenter,
                  SideLeft,
                  SideRight,
                  Unknown, // BackTopCenter
                  Unknown, // TopFrontLeft
                  Unknown, // TopFrontCenter
                  Unknown, // TopFrontRight
                  Unknown, // TopRearLeft
                  Unknown, // TopRearCenter
                  Unknown, // TopRearRight
               };
               ChannelInfo info = Unknown;
               if (label > 0 && label < ARRAY_SIZE(earlyMappings))
                  info = earlyMappings[label];
               else
               {
                  switch (label)
                  {
                  case kAudioChannelLabel_RearSurroundLeft:
                  case kAudioChannelLabel_RearSurroundRight:
                  case kAudioChannelLabel_LeftWide:
                  case kAudioChannelLabel_RightWide:
                  case kAudioChannelLabel_LFE2:
                  case kAudioChannelLabel_LeftTotal:
                  case kAudioChannelLabel_RightTotal:
                  case kAudioChannelLabel_HearingImpaired:
                  case kAudioChannelLabel_Narration:
                  case kAudioChannelLabel_Mono:
                  case kAudioChannelLabel_DialogCentricMix:
                  case kAudioChannelLabel_CenterSurroundDirect:
                  case kAudioChannelLabel_Haptic:
                  case kAudioChannelLabel_Ambisonic_W:
                  case kAudioChannelLabel_Ambisonic_X:
                  case kAudioChannelLabel_Ambisonic_Y:
                  case kAudioChannelLabel_Ambisonic_Z:
                  case kAudioChannelLabel_MS_Mid:
                  case kAudioChannelLabel_MS_Side:
                  case kAudioChannelLabel_XY_X:
                  case kAudioChannelLabel_XY_Y:
                  case kAudioChannelLabel_HeadphonesLeft:
                  case kAudioChannelLabel_HeadphonesRight:
                  case kAudioChannelLabel_ClickTrack:
                  case kAudioChannelLabel_ForeignLanguage:
                  case kAudioChannelLabel_Discrete:
                     break;
                  }

                  if (label >= kAudioChannelLabel_Discrete_0 && label <= kAudioChannelLabel_Discrete_65535)
                     ;
               }

               try
               {
                  mapping.push_back(info);
               }
               catch (const std::bad_alloc &)
               {
                  ERROR_SET(err, nomem);
               }
            }

            if (mapping.size())
            {
               try
               {
                  res->ChannelMap = std::make_shared<std::vector<ChannelInfo>>();
                  *res->ChannelMap = std::move(mapping);
               }
               catch (const std::bad_alloc &)
               {
                  ERROR_SET(err, nomem);
               }
            }
         }
         else
         {
            // XXX: duplicated from ALAC
            const ChannelInfo *channels = nullptr;
            int nc = 0;

#define CASE(NCHANNELS,...)                                   \
            case NCHANNELS:                                         \
            {                                                       \
               static const audio::ChannelInfo arr[] = __VA_ARGS__; \
               channels = arr;                                      \
               nc = ARRAY_SIZE(arr);                                \
            }                                                       \
            break

            switch (layout->mChannelLayoutTag)
            {
            // From ALAC:
            //
            CASE(kAudioChannelLayoutTag_MPEG_3_0_B, { FrontCenter, FrontLeft, FrontRight });
            CASE(kAudioChannelLayoutTag_MPEG_4_0_B, { FrontCenter, FrontLeft, FrontRight, RearCenter });
            CASE(kAudioChannelLayoutTag_MPEG_5_0_D, { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight });
            CASE(kAudioChannelLayoutTag_MPEG_5_1_D, { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight,  LFE });
            CASE(kAudioChannelLayoutTag_AAC_6_1,    { FrontCenter, FrontLeft, FrontRight, RearLeft,  RearRight,  RearCenter, LFE });
            CASE(kAudioChannelLayoutTag_MPEG_7_1_B, { FrontCenter, SideLeft,  SideRight,  FrontLeft, FrontRight, RearLeft, RearRight, LFE });

            // Other interesting ones:
            //
            CASE(kAudioChannelLayoutTag_MPEG_3_0_A, { FrontLeft, FrontRight, FrontCenter });
            CASE(kAudioChannelLayoutTag_MPEG_4_0_A, { FrontLeft, FrontRight, FrontCenter, RearCenter });
            CASE(kAudioChannelLayoutTag_MPEG_5_0_A, { FrontLeft, FrontRight, FrontCenter, RearLeft,  RearRight });
            CASE(kAudioChannelLayoutTag_MPEG_5_0_B, { FrontLeft, FrontRight, RearLeft,  RearRight, FrontCenter });
            CASE(kAudioChannelLayoutTag_MPEG_5_0_C, { FrontLeft, FrontCenter, FrontRight, RearLeft,  RearRight });
            CASE(kAudioChannelLayoutTag_MPEG_5_1_A, { FrontLeft, FrontRight, FrontCenter, LFE, RearLeft, RearRight });
            CASE(kAudioChannelLayoutTag_MPEG_5_1_B, { FrontLeft, FrontRight, RearLeft, RearRight, FrontCenter, LFE });
            CASE(kAudioChannelLayoutTag_MPEG_5_1_C, { FrontLeft, FrontCenter, FrontRight, RearLeft, RearRight, LFE });
            CASE(kAudioChannelLayoutTag_MPEG_6_1_A, { FrontLeft, FrontRight, FrontCenter, LFE, RearLeft, RearRight, RearCenter });
            CASE(kAudioChannelLayoutTag_MPEG_7_1_A, { FrontLeft, FrontRight, FrontCenter, LFE, RearLeft, RearRight, SideLeft, SideRight });
            }

#undef CASE
            if (channels && nc)
            {
               ApplyChannelLayout(*res, channels, nc, err);
               ERROR_CHECK(err);
            }
         }
      }

   exit:
      free(layout);
   }

   int Read(void *buf, int len, error *err)
   {
      AudioBufferList list;

      auto &p = list.mBuffers[0];
      list.mNumberBuffers = 1;

      p.mData = buf;
      p.mDataByteSize = len;
      p.mNumberChannels = channels;

      UInt32 framesIn = len/blockAlign;
      OSStatus status = 0;
      int r = 0;

      status = ExtAudioFileRead(extFile, &framesIn, &list);
      if (status) ERROR_SET(err, osstatus, status);

      r = framesIn * blockAlign; 
   exit:
      return r;
   }

   void Seek(uint64_t pos, error *err)
   {
      OSStatus status = 0;
      status = ExtAudioFileSeek(
         extFile,
         pos * sampleRate / 10000000L
      ); 
      if (status) ERROR_SET(err, osstatus, status);
   exit:;
   }

   uint64_t GetPosition(error *err)
   {
      SInt64 r = 0;
      OSStatus status = 0;

      status = ExtAudioFileTell(extFile, &r); 
      if (status) ERROR_SET(err, osstatus, status);

      r = (r + tellFrameCorrection) * 10000000LL / sampleRate;
   exit:
      return r;
   }

   uint64_t GetDuration(error *err)
   {
      OSStatus status = 0;
      uint64_t lenInSamples = 0;
      uint64_t r = 0;
      UInt32 metaLength = sizeof(lenInSamples);

      if (cachedDuration)
      {
         r = cachedDuration;
         goto exit;
      }

      status = ExtAudioFileGetProperty(
         extFile,
         kExtAudioFileProperty_FileLengthFrames,
         &metaLength,
         &lenInSamples
      );
      if (status) ERROR_SET(err, osstatus, status);

      r = lenInSamples * 10000000LL / sampleRate;
      cachedDuration = r;
   exit:
      return r;
   }

   void GetStreamInfo(audio::StreamInfo *info, error *err)
   {
      info->DurationKnown = cachedDuration != 0;

      stream->GetStreamInfo(&info->FileStreamInfo, err);
      ERROR_CHECK(err);

      Source::GetStreamInfo(info, err);
      ERROR_CHECK(err);
   exit:;
   }

private:

   const char *DescribeFormat(AudioFormatID id, char buf[5])
   {
      switch (id)
      {
      case kAudioFormatLinearPCM:
         return "PCM";
      case kAudioFormatAC3:
         return "AC3";
      case kAudioFormatEnhancedAC3:
         return "EAC3";
      case kAudioFormat60958AC3:
         return "AC3 60958";
      case kAudioFormatAppleIMA4:
         return "IMA4";
      case kAudioFormatMPEG4AAC:
         return "AAC";
      case kAudioFormatMPEG4CELP:
         return "CELP";
      case kAudioFormatMPEG4HVXC:
         return "HVXC";
      case kAudioFormatMPEG4TwinVQ:
         return "TwinVQ";
      case kAudioFormatMACE3:
         return "MACE 3:1";
      case kAudioFormatMACE6:
         return "MACE 6:1";
      case kAudioFormatULaw:
         return "uLaw";
      case kAudioFormatALaw:
         return "aLaw";
      // ...
      case kAudioFormatMPEGLayer1:
         return "MPEG layer 1";
      case kAudioFormatMPEGLayer2:
         return "MPEG layer 2";
      case kAudioFormatMPEGLayer3:
         return "MPEG layer 3";
      // ...
      case kAudioFormatAppleLossless:
         return "ALAC";
      // ...
      case kAudioFormatAMR:
         return "AMR";
      case kAudioFormatAMR_WB:
         return "AMR-WB";
      }

      buf[0] = (id >> 24);
      buf[1] = (id >> 16);
      buf[2] = (id >> 8);
      buf[3] = id;
      buf[4] = 0;

      return buf;
   }

   OSStatus Read(SInt64 pos, UInt32 len, void *buf, UInt32 *bytesIn)
   {
      error err;

      stream->Seek(pos, SEEK_SET, &err);
      ERROR_CHECK(&err);

      *bytesIn = stream->Read(buf, len, &err);
      ERROR_CHECK(&err);

   exit:
      return ERROR_FAILED(&err) ? ioErr : noErr;
   } 

   SInt64 GetSize(void)
   {
      SInt64 r = -1;
      error err;

      r = stream->GetSize(&err);
      ERROR_CHECK(&err);
   exit:
      return r;
   }

   static OSStatus ReadStatic(
      void *thisp_,
      SInt64 pos,
      UInt32 len,
      void *buf,
      UInt32 *bytesIn
   )
   {
      auto thisp = reinterpret_cast<CoreAudioSource*>(thisp_);
      return thisp->Read(pos, len, buf, bytesIn);
   }

   static SInt64 GetSizeStatic(void *thisp_)
   {
      auto thisp = reinterpret_cast<CoreAudioSource*>(thisp_);
      return thisp->GetSize();
   }
};

struct CoreAudioFactory : public Codec
{
   void TryOpen(
      Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   )
   {
      Pointer<CoreAudioSource> src;
      try
      {
         *src.GetAddressOf() = new CoreAudioSource();
      }
      catch(const std::bad_alloc &)
      {
         ERROR_SET(err, nomem);
      }
      src->Initialize(file, params, err);
      ERROR_CHECK(err);

      *obj = src.Detach();
      return;
   exit:;
   }
};

} // namespace

void audio::RegisterCoreAudioCodec(error *err)
{
   Pointer<CoreAudioFactory> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
   RegisterCodec(p.Get(), err);
   ERROR_CHECK(err);
exit:;
}
