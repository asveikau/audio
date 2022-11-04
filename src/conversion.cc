/*
 Copyright (C) 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioTransform.h>

#include <stdint.h>
#include <string.h>

#include <vector>

using namespace common;
using namespace audio;

namespace {

static const int little_endian = 1;

template <typename SrcReader, typename DstWriter>
struct GenericConverter : public Transform
{
   std::vector<unsigned char> conversionBuf;

   void
   TransformAudioPacket(void *&buf, size_t &len, error *err)
   {
      size_t srcPackets = len / (SrcReader::Bps / 8);
      size_t desiredSize = srcPackets * (DstWriter::Bps / 8);
      auto p = (const unsigned char*)buf;
      unsigned char *q;
      SrcReader reader;
      DstWriter writer;

      if (conversionBuf.size() < desiredSize)
      {
         try
         {
            conversionBuf.resize(desiredSize);
         }
         catch (const std::bad_alloc &)
         {
            ERROR_SET(err, nomem);
         }
      }

      q = conversionBuf.data();

      while (srcPackets)
      {
         writer((typename DstWriter::WriteType*)q, reader((const typename SrcReader::ReadType*)p));
         --srcPackets;
         p += SrcReader::Bps / 8;
         q += DstWriter::Bps / 8;
      }

      buf = conversionBuf.data();
      len = desiredSize;
   exit:;
   }
};

struct Pcm16Reader
{
   static const int Bps = 16;
   typedef int16_t ReadType;
   float operator()(const ReadType *p) { return *p / 32767.0f; }
};

struct Pcm24Reader
{
   static const int Bps = 24;
   typedef unsigned char ReadType;
   float operator()(const ReadType *p)
   {
      int32_t i = 0;
      char *q = ((char*)&i) + !*(char*)&little_endian;
      memcpy(q, p, 3);
      if (i & (1<<23))
      {
         uint32_t u = i;
         u |= 0xff000000U;
         i = u;
      }
      return i / 8388607.0f;
   }
};

struct PcmFloatReader
{
   static const int Bps = 32;
   typedef float ReadType;
   float operator()(const ReadType *p) { return *p; }
};

struct Pcm24PadReader
{
   static const int Bps = 32;
   typedef int32_t ReadType;
   float operator()(const ReadType *p)
   {
      return *p / 8388607.0f;
   }
};

struct Pcm16Writer
{
   static const int Bps = 16;
   typedef int16_t WriteType;
   void operator()(WriteType *p, float q)
   {
      *p = q * 32767.0f;
   }
};

struct Pcm24Writer
{
   static const int Bps = 24;
   typedef unsigned char WriteType;
   void operator()(WriteType *p, float q)
   {
      int32_t i = q * 8388607.0f;
      char *sp = ((char*)&i) + !*(char*)&little_endian;
      memcpy(p, sp, 3);
   }
};

struct Pcm24PadWriter
{
   static const int Bps = 32;
   typedef int32_t WriteType;
   void operator()(WriteType *p, float q)
   {
      *p = q * 8388607.0f;
   }
};

struct PcmFloatWriter
{
   static const int Bps = 32;
   typedef float WriteType;
   void operator()(WriteType *p, float q) { *p = q; }
};

struct Pcm24ToPcm24PadTransform : public Transform
{
   std::vector<uint32_t> conversionBuf;

   void
   TransformAudioPacket(void *&buf, size_t &len, error *err)
   {
      auto src = (const unsigned char*)buf;
      auto srclen = len;
      uint32_t *dst;
      size_t dstlen = srclen/3 * 4;

      if (conversionBuf.size() < dstlen)
      {
         try
         {
            conversionBuf.resize(dstlen);
         }
         catch (const std::bad_alloc &)
         {
            ERROR_SET(err, nomem);
         }
      }

      dst = conversionBuf.data();
      src += !*(char*)&little_endian;

      while (srclen >= 3)
      {
         memcpy((char*)dst + !*(char*)&little_endian, src, 3);
         src += 3;
         srclen -= 3;

         if (*dst & (1<<23))
            *dst |= 0xff000000U;
         else
            *dst &= ~0xff000000U;

         ++dst;
      }

      buf = conversionBuf.data();
      len = dstlen;
   exit:;
   }
};

struct Pcm24PadToPcm24Transform : public Transform
{
   void
   TransformAudioPacket(void *&buf, size_t &len, error *err)
   {
      auto dst = (unsigned char*)buf;
      auto src = (const unsigned char*)dst + !*(char*)&little_endian;
      auto srclen = len;

      while (srclen >= 4)
      {
         memmove(dst, src, 3);
         dst += 3;
         src += 4;
         srclen -= 4;
      }

      len = dst - (unsigned char*)buf;
   }
};

} // end namespace

Transform*
audio::CreateFormatConversion(
   Metadata &md,
   Format targetFormat,
   error *err
)
{
   Transform *r = nullptr;

   switch (md.Format)
   {
   case PcmShort:
      switch (targetFormat)
      {
      case PcmShort: goto exit;
      case Pcm24:    r = new (std::nothrow) GenericConverter<Pcm16Reader, Pcm24Writer>(); goto postCtor;
      case Pcm24Pad: r = new (std::nothrow) GenericConverter<Pcm16Reader, Pcm24PadWriter>(); goto postCtor;
      case PcmFloat: r = new (std::nothrow) GenericConverter<Pcm16Reader, PcmFloatWriter>(); goto postCtor;
      }
      break;
   case Pcm24:
      switch (targetFormat)
      {
      case PcmShort: r = new (std::nothrow) GenericConverter<Pcm24Reader, Pcm16Writer>(); goto postCtor;
      case Pcm24:    goto exit;
      case Pcm24Pad: r = new (std::nothrow) Pcm24ToPcm24PadTransform(); goto postCtor;
      case PcmFloat: r = new (std::nothrow) GenericConverter<Pcm24Reader, PcmFloatWriter>(); goto postCtor;
      }
      break;
   case Pcm24Pad:
      switch (targetFormat)
      {
      case PcmShort: r = new (std::nothrow) GenericConverter<Pcm24PadReader, Pcm16Writer>(); goto postCtor;
      case Pcm24:    r = new (std::nothrow) Pcm24PadToPcm24Transform(); goto postCtor;
      case Pcm24Pad: goto exit;
      case PcmFloat: r = new (std::nothrow) GenericConverter<Pcm24PadReader, PcmFloatWriter>(); goto postCtor;
      }
      break;
   case PcmFloat:
      switch (targetFormat)
      {
      case PcmShort: r = new (std::nothrow) GenericConverter<PcmFloatReader, Pcm16Writer>(); goto postCtor;
      case Pcm24:    r = new (std::nothrow) GenericConverter<PcmFloatReader, Pcm24Writer>(); goto postCtor;
      case Pcm24Pad: r = new (std::nothrow) GenericConverter<PcmFloatReader, Pcm24PadWriter>(); goto postCtor;
      case PcmFloat: goto exit;
      }
      break;
   }

   ERROR_SET(err, unknown, "Unsupported format");
postCtor:
   if (!r)
      ERROR_SET(err, nomem);
   md.Format = targetFormat;
exit:
   return r;
}
