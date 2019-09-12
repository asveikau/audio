/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#import <AudioToolbox/AudioToolbox.h>

#include <AudioCodec.h>
#include "seekbase.h"

#include <common/c++/new.h>

using namespace common;
using namespace audio;

namespace {

class CoreAudioSource : public Source
{
   Pointer<Stream> stream;
   AudioFileID id;
   ExtAudioFileRef extFile;
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
      targetFormat.mBitsPerChannel = 16;

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
      res->Format = PcmShort;
      res->SampleRate = sampleRate;
      res->Channels = channels;
      res->SamplesPerFrame = 0;
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
      catch(std::bad_alloc)
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
