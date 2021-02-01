/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include <common/misc.h>
#include <common/c++/new.h>

#include "../../third_party/opencore-audio/gsm_amr/amr_nb/dec/src/gsmamr_dec.h"

void* AmrWb_D_IF_init(void);
void AmrWb_D_IF_decode(void* s, const unsigned char* in, short* out, int bfi);
void AmrWb_D_IF_exit(void* s);

#include "seekbase.h"

#include <string.h>
#include <errno.h>
#include <vector>

using namespace common;
using namespace audio;

namespace {

class AmrSource : public Source, public SeekBase
{
   Pointer<Stream> stream;
   int sampleRate;
   const int *sizeTable;
   int sizeTableLen;
   int maxPacketSize;
   bool eof;
   uint64_t startOfData;
   uint64_t currentPos;
   std::vector<unsigned char> readBuffer;

   uint64_t
   SamplesToUnits(uint64_t samples)
   {
      return (samples * 10000000LL / sampleRate);
   }

   void
   ReadFrame(error *err)
   {
      int cmr = 0;
      auto r = stream->Read(readBuffer.data(), 1, err);
      ERROR_CHECK(err);
      if (!r)
         goto got_eof;

      cmr = ((readBuffer[0] >> 3) & 0x0f);

      if (cmr < 0 || cmr >= sizeTableLen)
         ERROR_SET(err, unknown, "Invalid AMR header"); 

      r = stream->Read(readBuffer.data() + 1, sizeTable[cmr], err);
      ERROR_CHECK(err);
      if (r != sizeTable[cmr])
         goto got_eof;

   exit:
      return;
   got_eof:
      eof = true;
   }

protected:
   virtual void Decode(const void *inBuffer, int cmr, void *outBuffer) = 0;
public:

   AmrSource(
      Stream *stream_,
      int rate,
      const int *sizes,
      int nSizes,
      uint64_t duration
   ) :
      SeekBase(duration),
      stream(stream_),
      sampleRate(rate),
      sizeTable(sizes),
      sizeTableLen(nSizes),
      eof(false),
      startOfData(0),
      currentPos(0)
   {
      ContainerHasSlowSeek = true;

      maxPacketSize = *sizes++;
      for (--nSizes; nSizes--; ++sizes)
      {
         if (*sizes > maxPacketSize)
           maxPacketSize = *sizes;
      }
      ++maxPacketSize;
      readBuffer.resize(maxPacketSize);
   }

   virtual void Initialize(error *err)
   {
      startOfData = stream->GetPosition(err);
   }

   void GetMetadata(Metadata *metadata, error *err)
   {
      metadata->Format = PcmShort;
      metadata->Channels = 1;
      metadata->SampleRate = sampleRate;
      metadata->SamplesPerFrame = 20 * sampleRate / 1000;
   }

   int Read(void *buf, int len, error *err)
   {
      int r = 0;

      if (eof)
         return 0;

      if (len < 2 * 20 * sampleRate / 1000)
         ERROR_SET(err, unknown, "This codec wants frame at a time decode");

      Decode(readBuffer.data(), (readBuffer[0] >> 3) & 0x0f, buf);

      r = 20 * sampleRate / 1000;
      currentPos += SamplesToUnits(r);
      r *= 2;

      ReadFrame(err);

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

   void GetStreamInfo(audio::StreamInfo *info, error *err)
   {
      info->DurationKnown = SeekBase::GetDurationKnown();

      stream->GetStreamInfo(&info->FileStreamInfo, err);
      ERROR_CHECK(err);

      Source::GetStreamInfo(info, err);
      ERROR_CHECK(err);
   exit:;
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
      return eof ? 0 : (20 * 10000000LL / 1000);
   }

   void SeekToOffset(uint64_t off, uint64_t time, error *err)
   {
      currentPos = time;
      eof = false;

      stream->Seek(startOfData + off, SEEK_SET, err);
      ERROR_CHECK(err);

      ReadFrame(err);
      ERROR_CHECK(err);

   exit:;
   }

   void SkipFrame(error *err)
   {
      if (!eof)
      {
         ReadFrame(err);
         ERROR_CHECK(err);
      }
   exit:;
   }

   void CapturePosition(RollbackBase **rollback, error *err)
   {
      *rollback = CreateRollbackWithCursorPos(
         stream.Get(), err,
         currentPos, eof, readBuffer
      );
   }
};


const int NarrowFrameSizes[] =
{
   12, 13, 15, 17, 19, 20, 26, 31, 5
};

class AmrNbSource : public AmrSource
{
   void *pCtx;
public:

   AmrNbSource(Stream *stream_, uint64_t duration)
      : AmrSource(
           stream_,
           8000,
           NarrowFrameSizes,
           ARRAY_SIZE(NarrowFrameSizes),
           duration
        ),
        pCtx(nullptr)
   {
   }

   ~AmrNbSource()
   {
      if (pCtx)
         GSMDecodeFrameExit(&pCtx);
   } 

   const char *Describe(void) { return "[opencore] AMR"; }

   void Initialize(error *err)
   {
      AmrSource::Initialize(err);
      ERROR_CHECK(err);

      if (-1 == GSMInitDecode(&pCtx, (Word8*)"AmrNbSource"))
         ERROR_SET(err, nomem);

   exit:;
   }

protected:

   void Decode(const void *inBuffer, int cmr, void *outBuffer) 
   { 
      AMRDecode(
         pCtx,
         (Frame_Type_3GPP)cmr,
         ((UWord8*)inBuffer) + 1,
         (Word16*)outBuffer,
         MIME_IETF
      );
   }
};

const int WideFrameSizes[] =
{
   17, 23, 32, 36, 40, 46, 50, 58, 60, 5
};

class AmrWbSource : public AmrSource
{
   void *pCtx;

public:

   AmrWbSource(Stream *stream_, uint64_t duration)
      : AmrSource(
           stream_,
           16000,
           WideFrameSizes,
           ARRAY_SIZE(WideFrameSizes),
           duration
        ),
        pCtx(nullptr)
   {
   }

   ~AmrWbSource()
   {
      if (pCtx)
         AmrWb_D_IF_exit(pCtx);
   }

   const char *Describe(void) { return "[opencore] AMR-WB"; }

   void Initialize(error *err)
   {
      AmrSource::Initialize(err);
      ERROR_CHECK(err);

      pCtx = AmrWb_D_IF_init(); 
      if (!pCtx)
         ERROR_SET(err, nomem);
   exit:;
   }

protected:

   void Decode(const void *inBuffer, int cmr, void *outBuffer)
   {
      AmrWb_D_IF_decode(pCtx, (unsigned char*)inBuffer, (int16_t*)outBuffer, 0);
   }
};

struct AmrCodec : public Codec
{
   const int MagicLen = 9;
   int GetBytesRequiredForDetection() { return MagicLen; }

   void TryOpen(
      common::Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   )
   {
      Pointer<AmrSource> r;
      static const char nbMagic[] = "#!AMR\n";
      static const char wbMagic[] = "#!AMR-WB\n";
      const char *p = (const char*)firstBuffer;

      try
      {
         if (!strncmp(p, nbMagic, sizeof(nbMagic)-1))
         {
            file->Seek(sizeof(nbMagic)-1, SEEK_CUR, err);
            ERROR_CHECK(err);
            *r.GetAddressOf() = new AmrNbSource(file, params.Duration);
         }
         else if (!strncmp(p, wbMagic, sizeof(wbMagic)-1))
         {
            file->Seek(sizeof(wbMagic)-1, SEEK_CUR, err);
            ERROR_CHECK(err);
            *r.GetAddressOf() = new AmrWbSource(file, params.Duration);
         }
      }
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }

      if (!r.Get())
         goto exit;

      r->Initialize(err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *obj = r.Detach();
   }
};

} // namespace

void audio::RegisterOpenCoreAmrCodec(error *err)
{
   Pointer<AmrCodec> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
   RegisterCodec(p.Get(), err);
   ERROR_CHECK(err);
exit:;
}
