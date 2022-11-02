/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include <common/misc.h>
#include <common/logger.h>
#include <common/c++/new.h>

#include "seekbase.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <pvmp4audiodecoder_api.h>

using namespace common;
using namespace audio;

namespace {

const int HEADER_SIZE = 7;

struct ParsedFrameHeader
{
   int MpegVersion;
   int SampleRate;
   int Channels;
   int FrameSize;
   int SamplesPerFrame;
};

bool IsSyncWord(const unsigned char *p)
{
   return (p[0] == 0xff) && ((p[1] & 0xf0) == 0xf0);
}

void ParseHeader(
   const unsigned char header[HEADER_SIZE],
   ParsedFrameHeader& parsed,
   error *err
)
{
   static const int SampleRates[] =
   {
      96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000,
      12000, 11025,  8000,  7350,
   }; 
   static const int Channels[] =
   {
      0, 1, 2, 3, 4, 5, 6, 8
   };
   int layer = 0, sampleRate = 0, channels = 0;

   if (!IsSyncWord(header))
      ERROR_SET(err, unknown, "Bad frame header - no sync word");

   layer = (header[1] >> 1) & 0x3;
   if (layer != 0)
      ERROR_SET(err, unknown, "Not an AAC header - perhaps MP3?");

   parsed.MpegVersion = ((header[1]>>3)&1) ? 4 : 2;

   sampleRate = (header[2] >> 2) & 0xf;
   if (sampleRate >= ARRAY_SIZE(SampleRates))
      ERROR_SET(err, unknown, "Invalid sample rate");

   parsed.SampleRate = SampleRates[sampleRate];

   channels = ((header[2] & 1) << 2) | (header[3] >> 6);
   if (channels == 0 || channels >= ARRAY_SIZE(Channels))
      ERROR_SET(err, unknown, "Invalid channel configuration");

   parsed.Channels = Channels[channels];

   parsed.SamplesPerFrame = 1024 * ((header[6] & 3) + 1);

   // XXX, the decoder seems to assume a stereo output buffer
   // for mono files.
   //
   if (parsed.Channels == 1)
      parsed.SamplesPerFrame *= 2;

   parsed.FrameSize = 
      (((uint32_t)(header[3] & 3)) << 11) |
      (((uint32_t)header[4]) << 3) |
      (header[5] >> 5);

   if (parsed.FrameSize < HEADER_SIZE)
      ERROR_SET(err, unknown, "ADTS frame too small");
exit:;
}

class AacSource : public Source, public SeekBase
{
   void *pMem;
   tPVMP4AudioDecoderExternal decoderExt;
   Pointer<Stream> stream;
   ParsedFrameHeader lastHeader;
   bool eof;
   uint64_t startOfData;
   uint64_t currentPos;
   unsigned char readBuffer[8192];
   char description[128];

public:

   AacSource(ParsedFrameHeader header, uint64_t duration) :
      SeekBase(duration),
      pMem(nullptr),
      lastHeader(header),
      eof(false),
      startOfData(0),
      currentPos(0)
   {
      ContainerHasSlowSeek = true;

      pMem = new char[PVMP4AudioDecoderGetMemRequirements()];
      memset(&decoderExt, 0, sizeof(decoderExt));
      PVMP4AudioDecoderInitLibrary(&decoderExt, pMem);
   }

   ~AacSource()
   {
      delete [] (char*)pMem;
   }

   const char *Describe(void)
   {
      snprintf(
         description,
         sizeof(description),
         "[opencore] AAC, MPEG-%d",
         lastHeader.MpegVersion
      );
      return description;
   }

   void Initialize(Stream *stream, error *err)
   {
      startOfData = stream->GetPosition(err);
      ERROR_CHECK(err);

      decoderExt.inputBufferMaxLength = sizeof(readBuffer);
      decoderExt.pInputBuffer = readBuffer;
      decoderExt.outputFormat = OUTPUTFORMAT_16PCM_INTERLEAVED;
      decoderExt.repositionFlag = decoderExt.aacPlusEnabled = TRUE;
      if (HEADER_SIZE != stream->Read(readBuffer, HEADER_SIZE, err))
         ERROR_SET(err, unknown, "Unexpectedly short read on first header");
      this->stream = stream;
   exit:;
   }

   void GetMetadata(Metadata *metadata, error *err)
   {
      metadata->Format = PcmShort;
      metadata->Channels = lastHeader.Channels;
      metadata->SampleRate = lastHeader.SampleRate;
      metadata->SamplesPerFrame = lastHeader.SamplesPerFrame;
   }

   int Read(void *buf, int len, error *err)
   {
      int r = 0;
      int32_t status = 0;
      int retryCount = 5;

      if (!len || eof)
         goto exit;

   retry:
      if (len < (lastHeader.SamplesPerFrame
                    * lastHeader.Channels
                    * GetBitsPerSample(PcmShort)/8))
      {
         ERROR_SET(
            err,
            unknown,
            "Codec wants entire frames. Buffer is too small."
         );
      }

      decoderExt.desiredChannels = lastHeader.Channels;
      decoderExt.inputBufferUsedLength = 0;
      decoderExt.inputBufferCurrentLength = HEADER_SIZE + stream->Read(
         readBuffer + HEADER_SIZE,
         lastHeader.FrameSize - HEADER_SIZE,
         err
      );
      ERROR_CHECK(err);

      decoderExt.pOutputBuffer = (Int16*)buf;

      status = PVMP4AudioDecodeFrame(&decoderExt, pMem);
      if (status != MP4AUDEC_SUCCESS)
         ERROR_SET(err, pvmp4, status);

      r = decoderExt.frameLength;
      currentPos += SamplesToUnits(r, lastHeader.SampleRate);
      r *= (GetBitsPerSample(PcmShort)/8) * lastHeader.Channels;

      ReadHeader(readBuffer, err);
     
      if (ERROR_FAILED(err)) { eof = true; error_clear(err); }
   exit:
      if (status)
      {
         if (retryCount--)
         {
            error_clear(err);
            status = 0;
            SkipFrame(err);
            if (!ERROR_FAILED(err) && !MetadataChanged)
               goto retry;
         }
         if (ERROR_FAILED(err)) { eof = true; error_clear(err); }
      }
      return r;
   }

   void Seek(uint64_t pos, error *err)
   {
      SeekBase::Seek(pos, err);
   }

   uint64_t GetPosition(error *err)
   {
      return GetPosition();
   }

   uint64_t GetDuration(error *err) 
   {
      return SeekBase::GetDuration(err);
   }

   void GetStreamInfo(audio::StreamInfo *info, error *err)
   {
      info->DurationKnown = SeekBase::GetDurationKnown();

      stream->GetStreamInfo(&info->FileStreamInfo, err);
      ERROR_CHECK(err);

      Source::GetStreamInfo(info, err);
      ERROR_CHECK(err);
   exit:;
   }

private:

   void ReadHeader(void *buf, error *err)
   {
      unsigned char header[HEADER_SIZE];
      size_t r = 0;
      int channels = lastHeader.Channels;
      int sampleRate = lastHeader.SampleRate;
      int samplesPerFrame = lastHeader.SamplesPerFrame;
      unsigned char *p = buf ? (unsigned char*)buf : header;

      r = stream->Read(p, sizeof(header), err);
      ERROR_CHECK(err);
      if (r != sizeof(header) || !IsSyncWord(p))
      {
         eof = true;
         goto exit;
      }

      ParseHeader(p, lastHeader, err);
      ERROR_CHECK(err);
      if (channels != lastHeader.Channels ||
          sampleRate != lastHeader.SampleRate ||
          samplesPerFrame != lastHeader.SamplesPerFrame)
      {
         MetadataChanged = true;
      }

   exit:;
   }

   uint64_t
   SamplesToUnits(uint64_t samples, uint64_t rate)
   {
      return (samples * 10000000LL / rate);
   }

   uint64_t
   GetDuration(const ParsedFrameHeader &header)
   {
      return SamplesToUnits(header.SamplesPerFrame, header.SampleRate);
   }

   void
   error_set_pvmp4(error *err, int32_t code)
   {
      const char *msg = "frame decode error";

      switch (code)
      {
      case MP4AUDEC_INVALID_FRAME:
         msg = "Invalid frame";
         break;
      case MP4AUDEC_INCOMPLETE_FRAME:
         msg = "Incomplete frame";
         break;
      case MP4AUDEC_LOST_FRAME_SYNC:
         msg = "Lost frame sync";
         break;
      }

      error_set_unknown(err, msg);
   }

protected:

   //
   // SeekBase methods
   //

   uint64_t GetPosition(void)
   {
      return currentPos;
   }

   uint64_t GetNextDuration(void)
   {
      return eof ? 0 : GetDuration(lastHeader);
   }

   void SeekToOffset(uint64_t off, uint64_t time, error *err)
   {
      currentPos = time;
      eof = false;

      stream->Seek(startOfData + off, SEEK_SET, err);
      ERROR_CHECK(err);

      ReadHeader(readBuffer, err);
      ERROR_CHECK(err);
   exit:;
   }

   void SkipFrame(error *err)
   {
      if (!eof)
      {
         stream->Seek(
            lastHeader.FrameSize - HEADER_SIZE,
            SEEK_CUR,
            err
         );
         ERROR_CHECK(err);

         currentPos += GetDuration(lastHeader);

         ReadHeader(readBuffer, err);
         ERROR_CHECK(err);
      }
   exit:;
   }

   void CapturePosition(RollbackBase **rollback, error *err)
   {
      *rollback = CreateRollbackWithCursorPos(
         stream.Get(), err,
         currentPos, eof, lastHeader, MetadataChanged,
         readBuffer[0], readBuffer[1], readBuffer[2], readBuffer[3],
         readBuffer[4], readBuffer[5], readBuffer[6]
      );
   }
};

struct AacCodec : public Codec
{
   void TryOpen(
      common::Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   )
   {
      ParsedFrameHeader header;
      Pointer<AacSource> r;
      const unsigned char *p = (const unsigned char *)firstBuffer;
      bool headerParsed = false;

      if (firstBufferSize >= HEADER_SIZE)
      {
         if (IsSyncWord(p))
         {
            // Try parsing this one.
            //
            ParseHeader(p, header, err);

            // Make sure the next one parses.
            // Eliminates false positives above.
            //
            if (!ERROR_FAILED(err))
            {
               unsigned char nextHeader[HEADER_SIZE];
               ParsedFrameHeader next;

               auto offsetToNext = header.FrameSize;
               if (offsetToNext + HEADER_SIZE <= firstBufferSize)
               {
                  memcpy(nextHeader, p + offsetToNext, HEADER_SIZE);
               }
               else
               {
                  uint64_t oldPos = file->GetPosition(err);
                  ERROR_CHECK(err);
                  file->Seek(offsetToNext, SEEK_CUR, err);
                  ERROR_CHECK(err);
                  if (HEADER_SIZE != file->Read(nextHeader, HEADER_SIZE, err))
                  {
                     ERROR_CHECK(err);
                     ERROR_SET(err, unknown, "short read");
                  }
                  file->Seek(oldPos, SEEK_SET, err);
                  ERROR_CHECK(err);
               }
               ParseHeader(nextHeader, next, err);
            }
            if (!ERROR_FAILED(err))
            {
               headerParsed = true;
            }
            error_clear(err);
         }
      }

      if (!headerParsed)
         goto exit;

      try
      {
         *r.GetAddressOf() = new AacSource(header, params.Duration);
      }
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }

      r->Initialize(file, err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *obj = r.Detach();
   }

};

} // namespace

void audio::CreateOpenCoreAacCodec(Codec **codec, error *err)
{
   Pointer<AacCodec> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err))
      p = nullptr;
   *codec = p.Detach();
}

