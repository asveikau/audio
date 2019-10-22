/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <MicroCodec.h>
#include "codecs/seekbase.h"
#include <common/c++/new.h>
#include <common/misc.h>
#include <vector>

using namespace audio;

namespace {

class UCSource : public Source, public SeekBase
{
   common::Pointer<MicroCodec> uc;
   common::Pointer<common::Stream> stream;
   MicroCodecDemux lastHeader;
   bool eof;
   uint64_t startOfData;
   uint64_t currentPos;
   std::vector<unsigned char> readBuffer;

public:

   UCSource(MicroCodec *uc_, common::Stream *stream_, uint64_t duration) :
      SeekBase(duration),
      uc(uc_),
      stream(stream_),
      eof(false),
      startOfData(0),
      currentPos(0)
   {
      ContainerHasSlowSeek = true;
   }

   const char *
   Describe(void)
   {
      return uc->Describe();
   }

   void
   Initialize(error *err)
   {
      startOfData = stream->GetPosition(err);
      ERROR_CHECK(err);

      ReadHeader(err);
      ERROR_CHECK(err);
   exit:;
   }

   void
   GetMetadata(Metadata *metadata, error *err)
   {
      uc->GetMetadata(metadata, err);
   }

   int
   Read(void *buf, int len, error *err)
   {
      int r = 0;

      if (!len || eof)
         goto exit;

      if (readBuffer.size() < lastHeader.FrameSize)
      {
         uint64_t sz = lastHeader.FrameSize;
         const unsigned int align = 1024;
         sz = MIN((sz + align - 1) / align * align, (1ULL<<32)-1);
         try
         {
            readBuffer.resize(sz);
         }
         catch (std::bad_alloc)
         {
            ERROR_SET(err, nomem);
         }
      }

      r = stream->Read(readBuffer.data(), lastHeader.FrameSize, err);
      ERROR_CHECK(err);
      if (!r)
      {
         eof = true;
         goto exit;
      }
      if (r < lastHeader.FrameSize)
         ERROR_SET(err, unknown, "Short read");

      r = uc->Decode(
         readBuffer.data(),
         r,
         buf,
         len,
         err
      );
      ERROR_CHECK(err);

      currentPos += GetDuration(lastHeader);

      ReadHeader(err);
      ERROR_CHECK(err);
   exit:
      if (ERROR_FAILED(err)) { eof = true; error_clear(err); r = 0; }
      return r;
   }

   void
   Seek(uint64_t pos, error *err)
   {
      SeekBase::Seek(pos, err);
   }

   uint64_t
   GetPosition(error *err)
   {
      return GetPosition();
   }

   uint64_t
   GetDuration(error *err)
   {
      return SeekBase::GetDuration(err);
   }

   void
   GetStreamInfo(audio::StreamInfo *info, error *err)
   {
      info->DurationKnown = SeekBase::GetDurationKnown();

      stream->GetStreamInfo(&info->FileStreamInfo, err);
      ERROR_CHECK(err);

      Source::GetStreamInfo(info, err);
      ERROR_CHECK(err);
   exit:;
   }

private:

   void
   ReadHeader(error *err)
   {
      int r = 0;

      r = stream->Read(&lastHeader, sizeof(lastHeader), err);
      ERROR_CHECK(err);
      if (r != sizeof(lastHeader))
      {
         eof = true;
         goto exit;
      }
   exit:;
   }

   uint64_t
   GetDuration(const MicroCodecDemux &header)
   {
      return header.Duration;
   }

protected:

   //
   // SeekBase methods
   //

   uint64_t
   GetPosition(void)
   {
      return currentPos;
   }

   uint64_t
   GetNextDuration(void)
   {
      return eof ? 0 : GetDuration(lastHeader);
   }

   void
   SeekToOffset(uint64_t off, uint64_t time, error *err)
   {
      currentPos = time;
      eof = false;

      stream->Seek(startOfData + off, SEEK_SET, err);
      ERROR_CHECK(err);

      ReadHeader(err);
      ERROR_CHECK(err);
   exit:;
   }

   void
   SkipFrame(error *err)
   {
      if (!eof)
      {
         stream->Seek(
            lastHeader.FrameSize,
            SEEK_CUR,
            err
         );
         ERROR_CHECK(err);

         currentPos += GetDuration(lastHeader);

         ReadHeader(err);
         ERROR_CHECK(err);
      }
   exit:;
   }

   void
   CapturePosition(RollbackBase **rollback, error *err)
   {
      *rollback = CreateRollbackWithCursorPos(
         stream.Get(), err,
         currentPos, eof, lastHeader, MetadataChanged
      );
   }
};

} // namespace

void
audio::AudioSourceFromMicroCodec(
   audio::MicroCodec *codec,
   common::Stream *demuxer,
   uint64_t duration,
   const void *config,
   int nbytes,
   Source **out,
   error *err
)
{
   common::Pointer<UCSource> r;

   codec->Initialize(config, nbytes, err);
   ERROR_CHECK(err);

   try
   {
      *r.GetAddressOf() = new UCSource(codec, demuxer, duration);
   }
   catch (std::bad_alloc)
   {
      ERROR_SET(err, nomem);
   }

   r->Initialize(err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err))
      r = nullptr;
   *out = r.Detach();
}