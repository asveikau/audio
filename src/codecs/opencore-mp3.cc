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

#include "pvmp3decoder_api.h"
#include "../../third_party/opencore-audio/mp3/dec/src/pvmp3_framedecoder.h"

#include "seekbase.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>

using namespace common;
using namespace audio;

namespace {

struct ParsedFrameHeader
{
   int MpegVersion;
   bool Mpeg25;
   int Layer;
   int Bitrate;
   int SampleRate;
   int Channels;
   int Padding;
   bool Protection;
   int FrameSize;
   int SamplesPerFrame;
};

bool IsSyncWord(const unsigned char *p)
{
   return (p[0] == 0xff) && ((p[1] & 0xe0) == 0xe0);
}

void ParseHeader(
   const unsigned char header[4],
   ParsedFrameHeader& parsed,
   error *err
)
{
   memset(&parsed, 0, sizeof(parsed));

   static const int bitrates[] =
   {
      // mpeg1, layer I
      32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,
      // mpeg1, layer II
      32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 
      // mpeg1, layer III
      32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 
      // mpeg2, layer I
      32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256,
      // mpeg2, layer II
      8,  16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160,
      // mpeg2, layer III
      8,  16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160,
   };
   static const int sampleRates[] =
   {
      // mpeg1
      44100, 48000, 32000,
      // mpeg2
      22050, 24000, 16000,
      // mpeg2.5
      11025, 12000,  8000,
   };
   static const int samplesPerFrame[] =
   {
      // mpeg1:
      // layer 1
      384,
      // layer 2
      1152,
      // layer 3
      1152,
      // mpeg 2:
      // layer 1
      384,
      // layer 2
      1152,
      // layer 3
      576,
   };
   unsigned char layer = 0, bitrate = 0, sampleRate = 0;

   if (!IsSyncWord(header))
      ERROR_SET(err, unknown, "Bad frame header - no sync word");

   parsed.MpegVersion = 4 - ((header[1] >> 3) & 3);
   parsed.Mpeg25 = ((header[1]&0xf0) == 0xe0);
   if (parsed.Mpeg25)
   {
      if (parsed.MpegVersion != 4)
         ERROR_SET(err, unknown, "Invalid MPEG version");
      parsed.MpegVersion = 2;
   }
   if (parsed.MpegVersion < 1 || parsed.MpegVersion > 2)
      ERROR_SET(err, unknown, "Invalid MPEG version");

   parsed.Protection = !(header[1]&1);

   layer = (header[1] >> 1) & 0x3;
   switch (layer)
   {
   case 1:
      parsed.Layer = 3;
      break;
   case 2:
      parsed.Layer = 2;
      break;
   case 3:
      parsed.Layer = 1;
      break;
   default:
      ERROR_SET(err, unknown, "Invalid layer");
   }

   if (parsed.Mpeg25 && parsed.Layer != 3)
      ERROR_SET(err, unknown, "Invalid layer for MPEG-2.5");

   bitrate = (header[2] >> 4);
   if (!bitrate || bitrate == 0xf)
      ERROR_SET(err, unknown, "Invalid bitrate");

   parsed.Bitrate = 
      (bitrates + (ARRAY_SIZE(bitrates)/2) * (parsed.MpegVersion-1)
                + (ARRAY_SIZE(bitrates)/6) * (parsed.Layer - 1))[bitrate - 1];

   sampleRate = (header[2] >> 2) & 0x3;
   if (sampleRate == 3)
      ERROR_SET(err, unknown, "Invalid sample rate");

   parsed.SampleRate =
      (sampleRates + ARRAY_SIZE(sampleRates)/3
                   * (parsed.MpegVersion-1 + (parsed.Mpeg25 ? 1 : 0)))
         [sampleRate];

   parsed.Padding = ((header[2] >> 1) & 1);

   parsed.Channels = ((header[3] >> 6) == 3) ? 1 : 2;

   parsed.SamplesPerFrame =
      (samplesPerFrame + ARRAY_SIZE(samplesPerFrame)/2
                       * (parsed.MpegVersion-1))
         [parsed.Layer - 1];

   parsed.FrameSize = parsed.SamplesPerFrame/8 * 1000
                         * parsed.Bitrate / parsed.SampleRate;

exit:;
}

class Mp3Source : public Source, public SeekBase
{
   void *pMem;
   tPVMP3DecoderExternal decoderExt;
   Pointer<Stream> stream;
   ParsedFrameHeader lastHeader;
   bool eof;
   uint64_t startOfData;
   uint64_t currentPos;
   unsigned char readBuffer[4096];
   char description[128];

public:

   Mp3Source(ParsedFrameHeader header, uint64_t duration) :
      SeekBase(duration),
      pMem(nullptr),
      lastHeader(header),
      eof(false),
      startOfData(0),
      currentPos(0)
   {
      pMem = new char[pvmp3_decoderMemRequirements()];
      memset(&decoderExt, 0, sizeof(decoderExt));
      pvmp3_InitDecoder(&decoderExt, pMem);
   }

   ~Mp3Source()
   {
      delete [] (char*)pMem;
   }

   const char *Describe(void)
   {
      snprintf(
         description,
         sizeof(description),
         "[opencore] MPEG-%d%s, layer %d, %d kbps",
         lastHeader.MpegVersion,
         lastHeader.Mpeg25 ? ".5" : "",
         lastHeader.Layer,
         lastHeader.Bitrate
      );
      return description;
   }

   void Initialize(Stream *stream, error *err)
   {
      startOfData = stream->GetPosition(err);
      ERROR_CHECK(err);

      decoderExt.inputBufferMaxLength = sizeof(readBuffer);
      decoderExt.pInputBuffer = readBuffer;
      if (4 != stream->Read(readBuffer, 4, err))
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

      if (!len || eof)
         goto exit;

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

      decoderExt.inputBufferUsedLength = 0;
      decoderExt.inputBufferCurrentLength = 4 + stream->Read(
         readBuffer + 4, 
         lastHeader.FrameSize + lastHeader.Padding - 4,
         err
      );
      ERROR_CHECK(err);

      decoderExt.pOutputBuffer = (int16*)buf;
      decoderExt.outputFrameSize = len/(2 * lastHeader.Channels);

      status = pvmp3_framedecoder(&decoderExt, pMem);
      if (status != NO_DECODING_ERROR)
         ERROR_SET(err, pvmp3, status);

      r = decoderExt.outputFrameSize;
      currentPos += SamplesToUnits(r, lastHeader.SampleRate);
      r *= 2 * lastHeader.Channels;

      ReadHeader(readBuffer, err);
      if (ERROR_FAILED(err)) { eof = true; error_clear(err); }
   exit:
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

private:

   void ReadHeader(void *buf, error *err)
   {
      unsigned char header[4];
      int r = 0;
      int channels = lastHeader.Channels;
      int sampleRate = lastHeader.SampleRate;
      int samplesPerFrame = lastHeader.SamplesPerFrame;
      unsigned char *p = buf ? (unsigned char*)buf : header;

      r = stream->Read(p, sizeof(header), err);
      ERROR_CHECK(err);
      if (r != sizeof(header))
      {
         eof = true;
         goto exit;
      }
      if (!IsSyncWord(p))
      {
rescan:
         unsigned char scanBuffer[4096];
         int nBytes;
         bool match = false;

         stream->Seek(0-sizeof(header), SEEK_CUR, err);
         ERROR_CHECK(err);

         nBytes = stream->Read(scanBuffer, sizeof(scanBuffer), err);
         ERROR_CHECK(err);
         if (nBytes < 4)
         {
            eof = true;
            goto exit;
         }

         for (int i=0; i<nBytes-4; ++i)
         {
            if (IsSyncWord(scanBuffer + i))
            {
               stream->Seek(0-(nBytes - i - 4), SEEK_CUR, err);
               ERROR_CHECK(err);
               memcpy(p, scanBuffer+i, 4);
               match = true;
               break;
            }
         }

         if (!match)
         {
            eof = true;
            goto exit;
         }
      }

      ParseHeader(p, lastHeader, err);
      if (ERROR_FAILED(err))
      {
         error_clear(err);
         stream->Seek(1, SEEK_CUR, err);
         ERROR_CHECK(err);
         goto rescan;
      }
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
   error_set_pvmp3(error *err, int32_t status)
   {
      const char *msg = "Decoder error";

      switch (status)
      {
      case UNSUPPORTED_LAYER:
         msg = "Unsupported layer";
         break;
      case UNSUPPORTED_FREE_BITRATE:
         msg = "Unsupported bitrate";
         break;
      case FILE_OPEN_ERROR:
         msg = "File open error";
         break;
      case CHANNEL_CONFIG_ERROR:
         msg = "Channel config error";
         break;
      case SYNTHESIS_WINDOW_ERROR:
         msg = "Error in synthesis window table";
         break;
      case READ_FILE_ERROR:
         msg = "Error reading file";
         break;
      case SIDE_INFO_ERROR:
         msg = "Error in side info";
         break;
      case HUFFMAN_TABLE_ERROR:
         msg = "Error in Huffman table";
         break;
      case MEMORY_ALLOCATION_ERROR:
         error_set_errno(err, ENOMEM);
         return;
      case NO_ENOUGH_MAIN_DATA_ERROR:
         msg = "Not enough data";
         break;
      case SYNCH_LOST_ERROR:
         msg = "Sync lost";
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

   void SeekToStart(error *err)
   {
      currentPos = 0;
      eof = false;

      stream->Seek(startOfData, SEEK_SET, err);
      ERROR_CHECK(err);

      ReadHeader(nullptr, err);
      ERROR_CHECK(err);
   exit:;
   }

   void SkipFrame(error *err)
   {
      if (!eof)
      {
         stream->Seek(
            lastHeader.FrameSize + lastHeader.Padding - 4,
            SEEK_CUR,
            err
         );
         ERROR_CHECK(err);

         currentPos += GetDuration(lastHeader);

         ReadHeader(nullptr, err);
         ERROR_CHECK(err);
      }
   exit:;
   }

   void CapturePosition(RollbackBase **rollback, error *err)
   {
      *rollback = CreateRollbackWithCursorPos(
         stream.Get(), err,
         currentPos, eof, lastHeader, MetadataChanged
      );
   }
};

struct Mp3Codec : public Codec
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
      Pointer<Mp3Source> r;
      const unsigned char *p = (const unsigned char *)firstBuffer;
      bool headerParsed = false;

      if (firstBufferSize >= 4)
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
               unsigned char nextHeader[4];
               ParsedFrameHeader next;

               auto offsetToNext = header.FrameSize + header.Padding;
               if (offsetToNext + 4 <= firstBufferSize)
               {
                  memcpy(nextHeader, p + offsetToNext, 4);
               }
               else
               {
                  uint64_t oldPos = file->GetPosition(err);
                  ERROR_CHECK(err);
                  file->Seek(offsetToNext, SEEK_CUR, err);
                  ERROR_CHECK(err);
                  if (4 != file->Read(nextHeader, 4, err))
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
         *r.GetAddressOf() = new Mp3Source(header, params.Duration);
      }
      catch (std::bad_alloc)
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

void audio::CreateOpenCoreMp3Codec(Codec **codec, error *err)
{
   Pointer<Mp3Codec> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err))
      p = nullptr;
   *codec = p.Detach();
}