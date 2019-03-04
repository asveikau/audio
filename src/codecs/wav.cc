/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <common/c++/new.h>

#include <AudioCodec.h>
#include <errno.h>
#include <string.h>

using namespace common;
using namespace audio;

namespace {

#pragma pack(1)
struct WavFormat
{
   uint16_t Format;
   uint16_t Channels;
   uint32_t SampleRate;
   uint32_t BytesPerSec;
   uint16_t BlockAlign;
   uint16_t BitsPerSample;
};
struct WavHeader
{
   uint32_t RiffMagic;
   uint32_t FileSize;
   uint32_t WaveMagic;
   uint32_t FmtMagic;
   uint32_t FormatHeaderSize;
   WavFormat FormatHeader;
};
struct Header
{
   uint32_t Tag;
   uint32_t Length;
};
#pragma pack()

const uint32_t kRiffMagic = 0x46464952;
const uint32_t kWaveMagic = 0x45564157;
const uint32_t kFmtMagic  = 0x20746d66;
const uint32_t kDataMagic = 0x61746164;

uint32_t
read32(const uint32_t *ptr)
{
   const unsigned char *p = (const unsigned char *)ptr;
   return *p |
          (((uint32_t)p[1]) << 8) |
          (((uint32_t)p[2]) << 16) |
          (((uint32_t)p[3]) << 24);
}

uint16_t
read16(const uint16_t *ptr)
{
   const unsigned char *p = (const unsigned char *)ptr;
   return *p | (((uint16_t)p[1]) << 8);
}

class WavSource : public Source
{
   Pointer<Stream> stream;
   Metadata metadata;
   uint64_t offsetToPayload;

public:

   const char *Describe(void) { return "[wav]"; }

   void Initialize(Stream *stream, error *err)
   {
      this->stream = stream;

      WavHeader header;
      const auto &fmt = header.FormatHeader;
      Header dataMagic;
      int nAttempts = 10;

      int r = stream->Read(&header, sizeof(header), err);
      ERROR_CHECK(err);
      if (r < sizeof(header))
         ERROR_SET(err, unknown, "WAV header too short");
      if (read32(&header.RiffMagic) != kRiffMagic ||
          read32(&header.WaveMagic) != kWaveMagic ||
          read32(&header.FmtMagic) != kFmtMagic)
         ERROR_SET(err, unknown, "Incorrect magic in WAV header");
      if (read32(&header.FormatHeaderSize) < sizeof(fmt))
         ERROR_SET(err, unknown, "WAV format header too short");
      stream->Seek(
         read32(&header.FormatHeaderSize) - sizeof(fmt),
         SEEK_CUR,
         err
      );
      ERROR_CHECK(err);

      memset(&dataMagic, 0, sizeof(dataMagic));

      do
      {
         if (!--nAttempts)
            ERROR_SET(err, unknown, "WAV header parse - could not find start of payload");
         stream->Seek(read32(&dataMagic.Length), SEEK_CUR, err);
         ERROR_CHECK(err);
         r = stream->Read(&dataMagic, sizeof(dataMagic), err);
         ERROR_CHECK(err);
         if (r != sizeof(dataMagic))
            ERROR_SET(err, unknown, "WAV header parse - short read");
      } while (read32(&dataMagic.Tag) != kDataMagic);

      offsetToPayload = stream->GetPosition(err);
      ERROR_CHECK(err);

      if (read16(&fmt.Format) != 1 || read16(&fmt.BitsPerSample) != 16)
         ERROR_SET(err, unknown, "Sorry - only 16-bit PCM supported");

      metadata.Format = PcmShort;
      metadata.Channels = read16(&fmt.Channels);
      metadata.SampleRate = read32(&fmt.SampleRate);
      metadata.SamplesPerFrame = 0;

   exit:;
   }

   void GetMetadata(Metadata *metadata, error *err)
   {
      *metadata = this->metadata;
   }

   int Read(void *buf, int len, error *err)
   {
      int r = stream->Read(buf, len, err);
      if (!ERROR_FAILED(err))
      {
         uint16_t *p = (uint16_t*)buf;
         int n = r/2;
         for (; n--; ++p)
            *p = read16(p);
      }
      return r;
   }

   void Seek(uint64_t pos, error *err)
   {
      auto sampleNo = pos * metadata.SampleRate / 10000000LL;
      stream->Seek(
         offsetToPayload + sampleNo * metadata.Channels * GetBitsPerSample(metadata.Format)/8,
         SEEK_SET,
         err
      );
   }

   uint64_t FilePosToTime(uint64_t r)
   {
      r -= offsetToPayload;
      r /= (metadata.Channels * GetBitsPerSample(metadata.Format)/8);
      return r * 10000000LL / metadata.SampleRate;
   }

   uint64_t GetPosition(error *err)
   {
      uint64_t r = stream->GetPosition(err);
      ERROR_CHECK(err);
      r = FilePosToTime(r);
   exit:
      return r;
   }

   uint64_t GetDuration(error *err)
   {
      uint64_t r = stream->GetSize(err);
      ERROR_CHECK(err);
      r = FilePosToTime(r);
   exit:
      return r;
   }
};

struct WavCodec : public Codec
{
   int GetBytesRequiredForDetection() { return sizeof(WavHeader); }

   void TryOpen(
      common::Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   )
   {
      auto header = reinterpret_cast<const WavHeader*>(firstBuffer);

      if (read32(&header->RiffMagic) == kRiffMagic &&
          read32(&header->WaveMagic) == kWaveMagic &&
          read32(&header->FmtMagic) == kFmtMagic)
      {
         Pointer<WavSource> r;

         New(r.GetAddressOf(), err);
         ERROR_CHECK(err);

         r->Initialize(file, err);
         ERROR_CHECK(err);

         *obj = r.Detach();
      }

      exit:;
   }

};

} // namespace

void audio::RegisterWavCodec(error *err)
{
   Pointer<WavCodec> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
   RegisterCodec(p.Get(), err);
   ERROR_CHECK(err);
exit:;
}
