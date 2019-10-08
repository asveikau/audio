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

#include <vector>
#include <functional>
#include <string>
#include <string.h>
#include <errno.h>

using namespace common;
using namespace audio;

namespace {

struct ParsedBoxHeader
{
   uint64_t Size;
   char Type[4];
};

uint16_t
read16(unsigned char i[2])
{
   return (((uint16_t)i[0]) << 8) | i[1];
}

uint32_t
read32(unsigned char i[4])
{
   return (((uint32_t)(i[0])) << 24)
        | (((uint32_t)(i[1])) << 16)
        | (((uint32_t)(i[2])) << 8)
        | i[3];
}

void
Read(
   Stream *file,
   void *buf,
   int len,
   uint64_t *limit,
   error *err
)
{
   int r = 0;

   if (limit && *limit < len)
      ERROR_SET(err, unknown, "Exceeded length");
   r = file->Read(buf, len, err);
   ERROR_CHECK(err);
   if (limit)
      *limit -= r;
   if (r != len)
      ERROR_SET(err, unknown, "Short read");
exit:;
}

void
ParseBoxHeader(
   Stream *file,
   uint64_t *limit,
   ParsedBoxHeader &header,
   error *err
)
{
   unsigned char size[4];
   uint32_t size32;
   bool lengthChecked = false;

   Read(file, size, sizeof(size), limit, err);
   ERROR_CHECK(err);

   size32 = read32(size);

   Read(file, header.Type, sizeof(header.Type), limit, err);
   ERROR_CHECK(err);

   if (size32 == 1)
   {
      unsigned char size64[8];
      Read(file, size64, sizeof(size64), limit, err);
      ERROR_CHECK(err);

      header.Size =
         (((uint64_t)read32(size64)) << 32) |
         read32(size64 + 4);

      if (header.Size < 16)
         ERROR_SET(err, unknown, "short box size");

      header.Size -= 16;
   }
   else if (size32 == 0)
   {
      if (limit)
         header.Size = *limit;
      else
      {
         uint64_t size, pos;

         size = file->GetSize(err);
         ERROR_CHECK(err);
         pos = file->GetPosition(err);
         ERROR_CHECK(err);

         header.Size = size - pos;

         lengthChecked = true;
      }
   }
   else
   {
      if (size32 < 8)
         ERROR_SET(err, unknown, "short box size");

      header.Size = size32 - 8;
   }

   if ((limit && header.Size > *limit) ||
       (!lengthChecked &&
        header.Size > file->GetSize(err)-file->GetPosition(err)))
   {
      ERROR_CHECK(err);
      ERROR_SET(err, unknown, "Box size surpasses limit");
   }
exit:;
}

struct Handler
{
   char Type[4];
   char Subtype[4];

   Handler()
   {
      memset(Type, 0, sizeof(Type));
      memset(Subtype, 0, sizeof(Subtype));
   }
};

struct StscEntry
{
   uint32_t FirstChunk;
   uint32_t SamplesPerChunk;
   uint32_t DescriptionIndex;
};

template<class Tkey, class Tvalue, class CMP>
Tvalue *
bsearch_nearest_match(
   const Tkey &key,
   const Tvalue *base,
   size_t nelem,
   CMP cmp
)
{
   long a = 0, b = nelem;
   Tvalue *ret = nullptr;
   Tvalue *elem_ptr = nullptr;

   do
   {
      long elem = a + (b-a)/2;

      elem_ptr = (Tvalue*)base + elem;

      int res = cmp(key, *elem_ptr);

      if (res > 0)
         a = elem + 1;
      else if (res < 0)
         b = elem - 1;
      else
      {
         ret = elem_ptr;
         break;
      }
   } while ((a < b) || (a == b && a < nelem));

   return ret ? ret : elem_ptr;
}


struct Track
{
   uint32_t Id;
   uint32_t TrackDuration;

   uint32_t TimeScale;
   uint64_t MediaDuration;
   uint16_t Language;
   Handler Handler;
   uint32_t DefaultSampleSize;
   uint32_t NumberOfSamples;
   std::vector<uint32_t> SampleSizes;
   std::vector<uint32_t> ChunkTable;
   std::vector<StscEntry> SamplesInChunk;
   bool ChunkTableIs64Bit;

   enum
   {
      Unrecognized,
      Aac,
      Mp3,
      AmrNb,
      AmrWb,
   } Codec;

   uint64_t CodecBoxOffset;
   uint64_t CodecBoxLength;

   const StscEntry *
   GetSamplesInChunk(int chunkNo)
   {
      ++chunkNo;

      auto r = bsearch_nearest_match(
         chunkNo,
         SamplesInChunk.data(),
         SamplesInChunk.size(),
         [] (const int a, const StscEntry &B) -> int
         {
            auto b = B.FirstChunk;
            if (a < b)
               return -1;
            if (a > b)
               return 1;
            return 0;
         }
      );
      if (!r)
         return r;
      while (r != SamplesInChunk.data() &&
             r->FirstChunk > chunkNo)
         --r;
      return r;
   }

   int
   GetNumChunks()
   {
      return ChunkTable.size() / (ChunkTableIs64Bit ? 2 : 1);
   }

   void
   GetChunkOffset(int idx, uint64_t *value, error *err)
   {
      if (idx < 0)
         ERROR_SET(err, unknown, "Chunk index cannot be negative");
      if (ChunkTableIs64Bit)
      {
         uint64_t *values = (uint64_t*)ChunkTable.data();
         int n = ChunkTable.size() / 2;
         if (idx >= n)
            ERROR_SET(err, unknown, "Invalid chunk index");
         *value = values[idx];
      }
      else
      {
         if (idx >= ChunkTable.size())
            ERROR_SET(err, unknown, "Invalid chunk index");

         *value = ChunkTable[idx];
      } 
   exit:;
   }

   uint32_t
   GetSampleSize(int idx, error *err)
   {
      if (idx < 0 || idx >= NumberOfSamples)
         ERROR_SET(err, unknown, "Invalid sample index");

      return DefaultSampleSize ? DefaultSampleSize : SampleSizes[idx]; 
   exit:
      return 0;
   }

   Track() :
      Id(0),
      TrackDuration(0),
      TimeScale(0),
      MediaDuration(0),
      Language(0),
      DefaultSampleSize(0),
      ChunkTableIs64Bit(false),
      Codec(Unrecognized),
      CodecBoxOffset(0),
      CodecBoxLength(0)
   {
   }
};

struct ParsedMoovBox
{
   uint32_t TimeScale;
   uint32_t Duration;
   std::vector<Track> Tracks;
   MetadataReceiver *Metadata;

   ParsedMoovBox() :
      TimeScale(0),
      Duration(0),
      Metadata(nullptr)
   {
   }
};

void
ParseBoxes(
   Stream *stream,
   uint64_t length,
   std::function<void(const ParsedBoxHeader &)> fn,
   error *err
)
{
   uint64_t pos = 0;

   while (length)
   {
      ParsedBoxHeader header;

      ParseBoxHeader(stream, &length, header, err);
      ERROR_CHECK(err);

      pos = stream->GetPosition(err);
      ERROR_CHECK(err);

      fn(header);

      ERROR_CHECK(err);

      stream->Seek(pos + header.Size, SEEK_SET, err);
      ERROR_CHECK(err);
      length -= header.Size;
   }
exit:;
}

void
ParseMvhd(
   Stream *stream,
   uint64_t length,
   struct ParsedMoovBox &moov,
   error *err
)
{
   unsigned char readBuf[8];

   if (length < 12+8)
      ERROR_SET(err, unknown, "moov box too short");

   stream->Seek(12, SEEK_CUR, err);
   ERROR_CHECK(err);

   Read(stream, readBuf, sizeof(readBuf), nullptr, err); 
   ERROR_CHECK(err);

   moov.TimeScale = read32(readBuf);
   moov.Duration = read32(readBuf + 4);

exit:;
}

void
ParseTkhd(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   unsigned char readBuf[4];

   if (length < 12+4+4+4)
      ERROR_SET(err, unknown, "tkhd box too short");

   stream->Seek(12, SEEK_CUR, err);
   ERROR_CHECK(err);

   Read(stream, readBuf, sizeof(readBuf), nullptr, err); 
   ERROR_CHECK(err);

   track.Id = read32(readBuf);

   stream->Seek(4, SEEK_CUR, err);
   ERROR_CHECK(err);

   Read(stream, readBuf, sizeof(readBuf), nullptr, err); 
   ERROR_CHECK(err);

   track.TrackDuration = read32(readBuf);
exit:;
}

void ParseHdlr(
   Stream *stream,
   uint64_t length,
   Handler &handler,
   error *err
)
{
   unsigned char readBuf[12];

   Read(stream, readBuf, sizeof(readBuf), &length, err);
   ERROR_CHECK(err);

   memcpy(handler.Type, readBuf + 4, 4);
   memcpy(handler.Subtype, readBuf + 8, 4);

exit:;
}

void
ParseStsz(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   unsigned char readBuf[12];
   uint32_t n = 0;

   Read(stream, readBuf, sizeof(readBuf), &length, err);
   ERROR_CHECK(err);

   track.DefaultSampleSize = read32(readBuf + 4);
   track.NumberOfSamples = n = read32(readBuf + 8);

   if (track.DefaultSampleSize)
      goto exit;

   while (n--)
   {
      Read(stream, readBuf, 4, &length, err);
      ERROR_CHECK(err);
      track.SampleSizes.push_back(read32(readBuf));
   }

exit:;
}

void
ParseStco(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   unsigned char readBuf[8];
   uint32_t n = 0;

   if (track.ChunkTable.size())
      ERROR_SET(err, unknown, "Chunk table specified twice?");

   Read(stream, readBuf, sizeof(readBuf), &length, err);
   ERROR_CHECK(err);
      
   n = read32(readBuf + 4);
   
   while (n--)
   {
      Read(stream, readBuf, 4, &length, err);
      ERROR_CHECK(err);
      track.ChunkTable.push_back(read32(readBuf));
   }

exit:;
}

void
ParseCo64(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   unsigned char readBuf[8];
   uint32_t n = 0;

   if (track.ChunkTable.size())
      ERROR_SET(err, unknown, "Chunk table specified twice?");

   track.ChunkTableIs64Bit = true;

   Read(stream, readBuf, sizeof(readBuf), &length, err);
   ERROR_CHECK(err);

   n = read32(readBuf + 4);

   while (n--)
   {
      union
      {
         uint64_t i64;
         uint32_t i32[2];
      } d;

      Read(stream, readBuf, 8, &length, err);
      ERROR_CHECK(err);

      d.i64 = read32(readBuf);
      d.i64 <<= 32;
      d.i64 |= read32(readBuf+4);

      track.ChunkTable.push_back(d.i32[0]);
      track.ChunkTable.push_back(d.i32[1]);
   }

exit:;
}

void
ReadEsDescriptor(Stream *stream, uint64_t &limit, unsigned char expectedTag, error *err)
{
   unsigned char tag = 0;
   uint32_t len = 0;

   Read(stream, &tag, 1, &limit, err);
   ERROR_CHECK(err);

   for (int i=0; i<4; ++i)
   {
      unsigned char ch = 0;

      Read(stream, &ch, 1, &limit, err);
      len <<= 7;
      len |= (ch & 0x7f);
      if (!(ch & 0x80))
         break;
   }

   if (tag != expectedTag)
      ERROR_SET(err, unknown, "Unexpected ES tag");
exit:;
}

void
ParseMp4a(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   unsigned char readBuffer[4];
   uint16_t version;
   uint32_t offset;

   if (length < 16)
      ERROR_SET(err, unknown, "mp4a box too short");

   length -= 16;
   stream->Seek(16, SEEK_CUR, err);
   ERROR_CHECK(err);

   Read(stream, readBuffer, 2, &length, err); 
   ERROR_CHECK(err);

   version = read16(readBuffer);

   switch (version)
   {
   case 1:
      if (length < 10)
         ERROR_SET(err, unknown, "mp4a box too short");
      length -= 10;
      stream->Seek(10, SEEK_CUR, err);
      ERROR_CHECK(err);
      break;
   case 2:
      if (length < 18)
         ERROR_SET(err, unknown, "mp4a box too short");
      length -= 18;
      stream->Seek(18, SEEK_CUR, err);
      ERROR_CHECK(err);
      Read(stream, readBuffer, 4, &length, err);
      ERROR_CHECK(err);
      offset = read32(readBuffer);
      length += 12;
      stream->Seek(-12, SEEK_CUR, err);
      ERROR_CHECK(err);
      if (length < offset)
         ERROR_SET(err, unknown, "mp4a box too short");
      stream->Seek(offset, SEEK_CUR, err);
      length -= offset;
      break;
   default:
      ERROR_SET(err, unknown, "mp4a box - unrecognized version");
   }

   ParseBoxes(
      stream,
      length,
      [&stream, &err, &track] (const ParsedBoxHeader &header)
      {
#undef ERROR_JMP
#define ERROR_JMP() goto innerExit
         if (!memcmp(header.Type, "esds", 4))
         {
            unsigned char ch;
            unsigned char dummy[4];
            uint64_t innerLen;

            track.CodecBoxOffset = stream->GetPosition(err);
            track.CodecBoxLength = header.Size;
            ERROR_CHECK(err);

            innerLen = header.Size;

            Read(stream, &dummy, 4, &innerLen, err);
            ERROR_CHECK(err);
    
            ReadEsDescriptor(stream, innerLen, 0x03, err);
            ERROR_CHECK(err);
            Read(stream, &dummy, 3, &innerLen, err);
            ERROR_CHECK(err);

            ReadEsDescriptor(stream, innerLen, 0x04, err);
            ERROR_CHECK(err);
      
            Read(stream, &ch, 1, &innerLen, err);
            ERROR_CHECK(err);

            switch (ch)
            {
            case 0x40:
            case 0x66:
            case 0x67:
            case 0x68:
               track.Codec = Track::Aac;
               break;
            case 0x69:
            case 0x6b:
               track.Codec = Track::Mp3;
               break;
            }
         }
      innerExit:;
#undef ERROR_JMP
#define ERROR_JMP() goto exit
      },
      err
   );
   ERROR_CHECK(err);

exit:;
}

void
ParseStsd(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   unsigned char readBuffer[8];
   uint32_t n = 0;
   Read(stream, readBuffer, sizeof(readBuffer), &length, err);
   ERROR_CHECK(err);
   n = read32(readBuffer + 4);
   while (n--)
   {
      ParsedBoxHeader header;
      uint64_t pos = 0;
      ParseBoxHeader(stream, &length, header, err);
      ERROR_CHECK(err);
      pos = stream->GetPosition(err);
      ERROR_CHECK(err);

      track.CodecBoxOffset = pos;
      track.CodecBoxLength = header.Size;

      if (!memcmp(header.Type, "mp4a", 4))
      {
         ParseMp4a(stream, header.Size, track, err);
         ERROR_CHECK(err); 
      }
      else if (!memcmp(header.Type, ".mp3", 4))
      {
         track.Codec = Track::Mp3;
      }
      else if (!memcmp(header.Type, "samr", 4))
      {
         track.Codec = Track::AmrNb;
      }
      else if (!memcmp(header.Type, "sawb", 4))
      {
         track.Codec = Track::AmrWb;
      }

      stream->Seek(pos + header.Size, SEEK_SET, err);
      ERROR_CHECK(err);
   }
exit:;
}

void
ParseStsc(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   unsigned char readBuffer[12];
   uint32_t n = 0;
   Read(stream, readBuffer, 8, &length, err);
   ERROR_CHECK(err);
   n = read32(readBuffer + 4);
   while (n--)
   {
      Read(stream, readBuffer, sizeof(readBuffer), &length, err);
      ERROR_CHECK(err);

      StscEntry entry; 

      entry.FirstChunk = read32(readBuffer);
      entry.SamplesPerChunk = read32(readBuffer + 4);
      entry.DescriptionIndex = read32(readBuffer + 8);

      track.SamplesInChunk.push_back(entry);
   }
exit:;
}

void
ParseStbl(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   ParseBoxes(
      stream,
      length,
      [&stream, &err, &track] (const ParsedBoxHeader &header)
      {
         if (!memcmp(header.Type, "stsz", 4))
            ParseStsz(stream, header.Size, track, err);
         else if (!memcmp(header.Type, "stco", 4))
            ParseStco(stream, header.Size, track, err);
         else if (!memcmp(header.Type, "co64", 4))
            ParseCo64(stream, header.Size, track, err);
         else if (!memcmp(header.Type, "stsd", 4)) 
            ParseStsd(stream, header.Size, track, err);
         else if (!memcmp(header.Type, "stsc", 4)) 
            ParseStsc(stream, header.Size, track, err);
      },
      err
   );
   ERROR_CHECK(err);
exit:;
}

void
ParseMdhd(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   unsigned char readBuf[14];
   unsigned char version = 0;

   Read(stream, &version, 1, &length, err);
   ERROR_CHECK(err); 

   switch (version)
   {
   case 0:
      if (length < 11 + 10)
         ERROR_SET(err, unknown, "mdhd box too short");
      stream->Seek(11, SEEK_CUR, err);
      ERROR_CHECK(err);
      length -= 11;

      Read(stream, readBuf, 10, &length, err);
      ERROR_CHECK(err);

      track.TimeScale = read32(readBuf);
      track.MediaDuration = read32(readBuf + 4);
      track.Language = read16(readBuf + 6);
      break;
   case 1:
      if (length < 19 + 14)
         ERROR_SET(err, unknown, "mdhd box too short");
      stream->Seek(19, SEEK_CUR, err);
      ERROR_CHECK(err);
      length -= 19;

      Read(stream, readBuf, 14, &length, err);
      ERROR_CHECK(err);
      track.TimeScale = read32(readBuf);
      track.MediaDuration = read32(readBuf + 4);
      track.MediaDuration <<= 32;
      track.MediaDuration |= read32(readBuf + 8);
      track.Language = read16(readBuf + 12);
      break;
   default:
      ERROR_SET(err, unknown, "Unsupported mdhd version");
   }

exit:;
}

void
ParseMinf(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   ParseBoxes(
      stream,
      length,
      [&stream, &err, &track] (const ParsedBoxHeader &header)
      {
         if (!memcmp(header.Type, "stbl", 4))
            ParseStbl(stream, header.Size, track, err);
      },
      err
   );
   ERROR_CHECK(err);
exit:;
}

void
ParseMdia(
   Stream *stream,
   uint64_t length,
   Track &track,
   error *err
)
{
   ParseBoxes(
      stream,
      length,
      [&stream, &err, &track] (const ParsedBoxHeader &header)
      {
         if (!memcmp(header.Type, "hdlr", 4))
            ParseHdlr(stream, header.Size, track.Handler, err);
         else if (!memcmp(header.Type, "minf", 4))
            ParseMinf(stream, header.Size, track, err);
         else if (!memcmp(header.Type, "mdhd", 4))
            ParseMdhd(stream, header.Size, track, err);
      },
      err
   );
   ERROR_CHECK(err);
exit:;
}

void
ParseTrak(
   Stream *stream,
   uint64_t length,
   struct ParsedMoovBox &moov,
   error *err
)
{
   Track track;

   ParseBoxes(
      stream,
      length,
      [&stream, &err, &track] (const ParsedBoxHeader &header)
      {
         if (!memcmp(header.Type, "tkhd", 4))
            ParseTkhd(stream, header.Size, track, err);
         else if (!memcmp(header.Type, "mdia", 4))
            ParseMdia(stream, header.Size, track, err);
      },
      err
   );
   ERROR_CHECK(err);

   moov.Tracks.push_back(track);
exit:;
}

void
ParseMetadataBox(
   Stream *stream,
   const ParsedBoxHeader &header,
   MetadataReceiver *recv,
   error *err
)
{
   auto onData =
      [stream, header, recv, &err] (uint64_t off, uint32_t len) -> void
      {
         enum TypeEnum
         {
            String,
            Integer,
            Binary,
         };
         struct Tag
         {
            TypeEnum DataType;
            int Enum;
            unsigned char Mp4Type[5];
         };
         static const Tag tags[] =
         {
            {String,  Title,         "\xa9""nam"},
            {String,  Album,         "\xa9""alb"},
            {String,  Artist,        "\xa9""ART"},
            {String,  Accompaniment, "aART"},
            {String,  Composer,      "\xa9""wrt"},
            {String,  ContentGroup,  "\xa9""grp"},
            {String,  Genre,         "\xa9""gen"},

            {Integer, Year,          "\xa9""day"},
         };

         for (auto p = tags; p < tags + ARRAY_SIZE(tags); ++p)
         {
            if (!memcmp(p->Mp4Type, header.Type, 4))
            {
               auto parse = [stream, off, len] (Stream **out, error *err) -> void
               {
                  stream->Substream(off, len, out, err);
               };
               auto parseString = [stream, off, len] (std::string &str, error *err) -> void
               {
                  std::vector<char> vec;
                  stream->Seek(off, SEEK_SET, err);
                  ERROR_CHECK(err);
                  try
                  {
                     vec.resize(len);
                     vec.resize(stream->Read(vec.data(), len, err));
                     ERROR_CHECK(err);
                     while (vec.size() && !vec[vec.size()-1])
                        vec.resize(vec.size()-1);
                     str = std::string(vec.data(), vec.size());
                  }
                  catch (std::bad_alloc)
                  {
                     ERROR_SET(err, nomem);
                  }
               exit:;
               };
               auto parseInt = [&parseString] (int64_t &i, error *err) -> void
               {
                  std::string str;
                  char *p = nullptr;

                  parseString(str, err);
                  if (!ERROR_FAILED(err))
                     i = strtoll(str.c_str(), &p, 10);
               };
               switch (p->DataType)
               {
               case String:
                  if (recv->OnString)
                  {
                     recv->OnString((StringMetadata)p->Enum, parseString, err);
                     ERROR_CHECK(err);
                  }
                  break;
               case Integer:
                  if (recv->OnInteger)
                  {
                     recv->OnInteger((IntegerMetadata)p->Enum, parseInt, err);
                     ERROR_CHECK(err);
                  }
                  break;
               case Binary:
                  if (recv->OnBinaryData)
                  {
                     recv->OnBinaryData((BinaryMetadata)p->Enum, parse, err);
                     ERROR_CHECK(err);
                  }
               }
               break;
            }
         }
      exit:;
      };
   ParseBoxes(
      stream,
      header.Size,
      [&stream, &err, onData] (const ParsedBoxHeader &header)
      {
         if (!memcmp(header.Type, "data", 4))
         {
            if (header.Size < 8)
               ERROR_SET(err, unknown, "Short atom");

            stream->Seek(8, SEEK_CUR, err);
            ERROR_CHECK(err);

            auto pos = stream->GetPosition(err);
            ERROR_CHECK(err);

            onData(pos, header.Size - 8);
         }
      exit:;
      },
      err
   );
}

void
ParseUdta(
   Stream *stream,
   uint64_t length,
   MetadataReceiver *recv,
   error *err
)
{
   ParseBoxes(
      stream, 
      length,
      [&stream, recv, &err] (const ParsedBoxHeader &header)
      {
         if (!memcmp(header.Type, "meta", 4))
         {
            if (header.Size < 4)
               ERROR_SET(err, unknown, "Short atom");

            stream->Seek(4, SEEK_CUR, err);
            ERROR_CHECK(err);

            ParseBoxes(
               stream,
               header.Size - 4,
               [&stream, recv, &err] (const ParsedBoxHeader &header)
               {
                  if (!memcmp(header.Type, "ilst", 4))
                  {
                     ParseBoxes(
                        stream,
                        header.Size,
                        [&stream, recv, &err] (const ParsedBoxHeader &header)
                        {
                           ParseMetadataBox(stream, header, recv, err);
                        },
                        err
                     );
                  }
               },
               err
            );
         }
      exit:;
      },
      err
   );
}

void
ParseMoov(
   Stream *stream,
   uint64_t length,
   ParsedMoovBox &moov,
   error *err
)
{
   ParseBoxes(
      stream, 
      length,
      [&stream, &err, &moov] (const ParsedBoxHeader &header)
      {
         if (!memcmp(header.Type, "mvhd", 4))
            ParseMvhd(stream, header.Size, moov, err);
         else if (!memcmp(header.Type, "trak", 4))
            ParseTrak(stream, header.Size, moov, err);
         else if (moov.Metadata && !memcmp(header.Type, "udta", 4))
            ParseUdta(stream, header.Size, moov.Metadata, err);
      },
      err
   );
}

struct ParsedMp4File
{
   ParsedMoovBox Moov;
   uint64_t MdatStart;
   uint64_t MdatLength;
};

struct Mp4ParseFinished {};

void ParseMp4File(
   Stream *stream,
   ParsedMp4File &file,
   error *err
)
{
   bool sawMoov = false, sawMdat = false;

   uint64_t len = stream->GetSize(err);
   ERROR_CHECK(err);

   try
   {
      ParseBoxes(stream, len, [&file, &err, &stream, &sawMoov, &sawMdat]
      (const ParsedBoxHeader &header)
      {
#undef ERROR_JMP
#define ERROR_JMP() goto innerExit
         if (!memcmp(header.Type, "moov", 4))
         {
            ParseMoov(stream, header.Size, file.Moov, err);
            sawMoov = true;
         }
         else if (!memcmp(header.Type, "mdat", 4))
         {
            file.MdatStart = stream->GetPosition(err);
            ERROR_CHECK(err);
            file.MdatLength = header.Size;
            sawMdat = true;
         }
         if (sawMoov && sawMdat)
            throw Mp4ParseFinished();
      innerExit:;
#undef ERROR_JMP
#define ERROR_JMP() goto exit
      }, err);
      ERROR_CHECK(err);
    }
    catch (std::bad_alloc)
    {
       ERROR_SET(err, nomem);
    }
    catch (Mp4ParseFinished)
    {
    }
   
exit:;
}

class Mp4DemuxStream : public Stream
{
   const void *fileHeader;
   int fileHeaderLen;
   int packetHeaderLen;
   uint64_t pos;
   uint64_t fileSize;
   std::vector<uint64_t> PacketStarts;
   int currentPacket;
   int currentChunk;
   int samplesWithinChunk;
   StscEntry *chunkLookup;

protected:

   Pointer<Stream> stream;
   ParsedMp4File mp4;
   Track& track;

   virtual void
   GetFileHeader(const void **buf, int *len, error *err)
   {
      *buf = nullptr;
      *len = 0;
   }

   virtual void
   GetPacketHeader(uint32_t packetLength, const void **buf, int *len, error *err)
   {
      *buf = nullptr;
      *len = 0;
   }

public:

   Mp4DemuxStream(Stream *stream_, ParsedMp4File&& mp4_, int trackIdx)
     : 
       pos(0),
       fileSize(0),
       currentPacket(0),
       currentChunk(0),
       samplesWithinChunk(0),
       chunkLookup(nullptr),
       stream(stream_),
       mp4(mp4_),
       track(mp4.Moov.Tracks[trackIdx])
   {
   }

   virtual void
   Initialize(error *err)
   {
      const void *dummyBuf;

      GetFileHeader(&fileHeader, &fileHeaderLen, err);
      ERROR_CHECK(err);

      GetPacketHeader(0, &dummyBuf, &packetHeaderLen, err);
      ERROR_CHECK(err);

      if (!track.NumberOfSamples)
         ERROR_SET(err, unknown, "No samples");

      if (!track.SamplesInChunk.size())
         ERROR_SET(err, unknown, "No stsc table");

      chunkLookup = track.SamplesInChunk.data();

      fileSize = fileHeaderLen;
      try
      {
         for (int i=0; i<track.NumberOfSamples; ++i)
         {
            PacketStarts.push_back(fileSize);
            fileSize += packetHeaderLen;
            fileSize += track.GetSampleSize(i, err);
            ERROR_CHECK(err);
         }
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }

   exit:;
   }

   uint64_t GetSize(error *err)
   {
      return fileSize;
   }

   uint64_t GetPosition(error *err)
   {
      return pos;
   }

   void Seek(int64_t pos, int whence, error *err)
   {
      uint64_t *nearestPacket;
      int chunks = 0;
      int pkt;

      switch (whence)
      {
      case SEEK_SET:
         break;
      case SEEK_CUR:
         Seek(this->pos + pos, SEEK_SET, err);
         return;
      case SEEK_END:
         Seek(fileSize + pos, SEEK_SET, err);
         return;
      default:
         ERROR_SET(err, unknown, "Bad seek whence");
      }

      // Set pos...
      //
      this->pos = MIN(fileSize, MAX(0LL, pos));

      if (this->pos == fileSize)
      {
         this->currentPacket = PacketStarts.size();
         return;
      }

      // Find sample...
      //
      nearestPacket = bsearch_nearest_match(
         this->pos,
         PacketStarts.data(),
         PacketStarts.size(),
         [] (uint64_t a, uint64_t b) -> int
         {
            if (a < b)
               return -1;
            if (a > b)
               return 1;
            return 0;
         } 
      );
      if (!nearestPacket)
         ERROR_SET(err, unknown, "Couldn't find packet");
      while (nearestPacket != PacketStarts.data() &&
             *nearestPacket > this->pos)
         --nearestPacket;
      currentPacket = nearestPacket - PacketStarts.data();

      //
      // Find chunk...
      //

      for (pkt = currentPacket,
              chunkLookup = track.SamplesInChunk.data(),
              currentChunk = 0,
              chunks = track.GetNumChunks();
           currentChunk < chunks;
           ++currentChunk)
      {
         AdvanceChunkLookup();
         if (pkt < chunkLookup->SamplesPerChunk)
         {
            samplesWithinChunk = pkt;
            break;
         }
         pkt -= chunkLookup->SamplesPerChunk;
      }

   exit:;
   }

   void
   AdvanceChunkLookup(void)
   {
      auto end = track.SamplesInChunk.data() +
                 track.SamplesInChunk.size();

      if (chunkLookup + 1 < end &&
          currentChunk + 1 >= chunkLookup[1].FirstChunk)
      {
         ++chunkLookup;
      }
   }

   int Read(void *buf, int len, error *err)
   {
      int r = 0;
      uint32_t currentPacketLen;
      uint32_t offset;
      uint64_t chunkOffset = 0;
      const void *packetHeader;

   retry:
      if (!len || currentPacket >= PacketStarts.size())
         goto exit;

      if (!currentPacket &&
          pos < fileHeaderLen)
      {
         int leftInHeader = fileHeaderLen - pos;
         int n = MIN(len, leftInHeader);
         if (n)
         {
            memcpy(buf, (const char*)fileHeader + pos, n);
            buf = (char*)buf + n;
            pos += n;
            r += n;
            len -= n;
            if (!len) goto exit;
         }
      }

      if (currentPacket + 1 == PacketStarts.size())
         currentPacketLen = fileSize - PacketStarts[currentPacket];
      else
         currentPacketLen = PacketStarts[currentPacket+1] - PacketStarts[currentPacket];

      offset = pos - PacketStarts[currentPacket];
      if (packetHeaderLen && offset < packetHeaderLen)
      {
         int headerRemaining = packetHeaderLen - offset;
         int n = MIN(headerRemaining, len);

         GetPacketHeader(currentPacketLen, &packetHeader, &packetHeaderLen, err);
         ERROR_CHECK(err);

         memcpy(buf, (const char*)packetHeader + offset, n);
         buf = (char*)buf + n;
         pos += n;
         r += n;
         offset += n;
         len -= n;
         if (!len) goto exit;
      }

      track.GetChunkOffset(currentChunk, &chunkOffset, err);
      ERROR_CHECK(err);

      for (int i=0; i<samplesWithinChunk; ++i)
      {
         int n = track.GetSampleSize(currentPacket - i - 1, err);
         ERROR_CHECK(err);
         chunkOffset += n;
      }

      {
         int n = track.GetSampleSize(currentPacket, err);
         int r2 = 0;
         ERROR_CHECK(err);
         offset -= packetHeaderLen;
         chunkOffset += offset;
         n -= offset;
         if (chunkOffset < mp4.MdatStart ||
             chunkOffset + n > mp4.MdatStart + mp4.MdatLength)
         {
            ERROR_SET(err, unknown, "chunk lies outside mdat box");
         }
         stream->Seek(chunkOffset, SEEK_SET, err);
         ERROR_CHECK(err);
         r2 = stream->Read(buf, MIN(n, len), err);
         ERROR_CHECK(err);
         if (r2 <= 0) goto exit;
         buf = (char*)buf + r2;
         len -= r2;
         n -= r2;
         r += r2;
         pos += r2;
         if (!n)
         {
            ++currentPacket;
            ++samplesWithinChunk;
            if (samplesWithinChunk >= chunkLookup->SamplesPerChunk)
            {
               ++currentChunk;
               samplesWithinChunk = 0;
            }
         }
         else
         {
            goto exit;
         }
      }

      AdvanceChunkLookup();

      if (len)
         goto retry;

   exit:
      return r;
   }

   void
   GetStreamInfo(common::StreamInfo *info, error *err)
   {
      stream->GetStreamInfo(info, err);
   }
};

class Mp4DemuxStreamWithSimpleHeader : public Mp4DemuxStream
{
   const void *header;
   int headerLen;
public:
   Mp4DemuxStreamWithSimpleHeader(
      Stream *stream,
      ParsedMp4File&& mp4,
      int trackIdx,
      const void *header_,
      int headerLen_
   )
      : Mp4DemuxStream(stream, std::move(mp4), trackIdx),
        header(header_),
        headerLen(headerLen_)
   {
   }
protected:
   void
   GetFileHeader(const void **buf, int *len, error *err)
   {
      *buf = header;
      *len = headerLen;
   }
};

class AacDemuxStream : public Mp4DemuxStream
{
   unsigned char adts[7];

public:
   AacDemuxStream(
      Stream *stream,
      ParsedMp4File&& mp4,
      int trackIdx
   )
      : Mp4DemuxStream(stream, std::move(mp4), trackIdx)
   {
      memset(&adts, 0, sizeof(adts));
   }

   void
   Initialize(error *err)
   {
      uint64_t length;
      unsigned char dummy[16];
      unsigned char config[0x2];
      unsigned char objectType;
      unsigned char freq;
      unsigned char channels;

      // Sync word + protection absent
      //
      adts[0] = 0xff;
      adts[1] = 0xf1;

      // Buffer fullness
      adts[5] = 0x1f;
      adts[6] = 0xfc;

      stream->Seek(track.CodecBoxOffset, SEEK_SET, err);
      ERROR_CHECK(err);
      length = track.CodecBoxLength;

      ::Read(stream.Get(), &dummy, 4, &length, err);
      ERROR_CHECK(err);

      ReadEsDescriptor(stream.Get(), length, 0x03, err);
      ERROR_CHECK(err);
      ::Read(stream.Get(), &dummy, 3, &length, err);
      ERROR_CHECK(err);

      ReadEsDescriptor(stream.Get(), length, 0x04, err);
      ERROR_CHECK(err);
      ::Read(stream.Get(), &dummy, 13, &length, err);
      ERROR_CHECK(err);

      ReadEsDescriptor(stream.Get(), length, 0x05, err);
      ERROR_CHECK(err);

      stream->Read(&config, sizeof(config), err);
      ERROR_CHECK(err);

      objectType = (config[0] >> 3);
      if (objectType == 31)
         ERROR_SET(err, unknown, "Object type not supported");
      freq = ((config[0] & 0x7) << 1) | (config[1] >> 7);
      if (freq == 15)
         ERROR_SET(err, unknown, "Sample rate not supported");
      channels = (config[1] & 0x7f) >> 3; 

      if (!objectType || objectType >= 4)
         ERROR_SET(err, unknown, "Object type not supported");
      if (!channels || channels >= 8)
         ERROR_SET(err, unknown, "Channel configuration not supported");

      adts[2] |= ((objectType - 1) << 6);
      adts[2] |= (freq << 2);
      adts[2] |= ((channels >> 2) ? 1 : 0);
      adts[3] |= (channels << 6);

      Mp4DemuxStream::Initialize(err);
      ERROR_CHECK(err);
   exit:;
   }

protected:

   void
   GetPacketHeader(uint32_t packetLength, const void **buf, int *len, error *err)
   {
      if (packetLength >= 16384)
         ERROR_SET(err, unknown, "Invalid ADTS packet length");

      adts[3] &= ~0x3;
      adts[4] = 0;
      adts[5] &= ~0xe0;

      adts[3] |= (packetLength >> 11); 
      adts[4] = (packetLength >> 3);
      adts[5] |= (packetLength&7) << 5;

      *buf = adts;
      *len = sizeof(adts);
   exit:;
   }
}; 

struct Mp4Codec : public Codec
{
   int GetBytesRequiredForDetection() { return 8; }

   void TryOpen(
      common::Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   )
   {
      ParsedMp4File mp4;
      Pointer<Mp4DemuxStream> demux;
      uint64_t pos = 0, duration = 0;
      const char *codecName = nullptr;

      if (memcmp((const char*)firstBuffer + 4, "ftyp", 4))
         goto exit;

      pos = file->GetPosition(err);
      ERROR_CHECK(err);

      mp4.Moov.Metadata = params.Metadata;

      ParseMp4File(file, mp4, err);
      ERROR_CHECK(err);

      try
      {
         for (int i=0; !demux.Get() && i<mp4.Moov.Tracks.size(); ++i)
         {
            auto &track = mp4.Moov.Tracks[i];

            switch (track.Codec)
            {
#if defined(USE_OPENCORE_AAC)
            case Track::Aac:
               codecName = "AAC";
               *demux.GetAddressOf() =
                  new AacDemuxStream(file, std::move(mp4), i);
               break;
#endif
#if defined(USE_OPENCORE_MP3)
            case Track::Mp3:
               codecName = "MP3";
               *demux.GetAddressOf() =
                  new Mp4DemuxStream(file, std::move(mp4), i);
               break;
#endif
#if defined(USE_OPENCORE_AMR)
            case Track::AmrNb:
               codecName = "AMR";
               *demux.GetAddressOf() =
                  new Mp4DemuxStreamWithSimpleHeader(
                     file,
                     std::move(mp4),
                     i,
                     "#!AMR\n",
                     6
                  );
               break;
            case Track::AmrWb:
               codecName = "AMR-WB";
               *demux.GetAddressOf() =
                  new Mp4DemuxStreamWithSimpleHeader(
                     file,
                     std::move(mp4),
                     i,
                     "#!AMR-WB\n",
                     9
                  );
               break;
#endif
            default:
               break;
            }

            duration = (demux.Get() && track.TimeScale) ?
                          track.MediaDuration * (10000000.0 / track.TimeScale) :
                          0;
         }
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }

      if (!demux.Get())
         goto exit;

      demux->Initialize(err);
      ERROR_CHECK(err);

      if (duration)
         params.Duration = duration;

      OpenCodec(demux.Get(), &params, obj, err);
      ERROR_CHECK(err);

      log_printf("mp4: Found %s track.", codecName);

   exit:;
   }
   
};

} // end namespace

void
audio::RegisterMp4Codec(error *err)
{
   Pointer<Mp4Codec> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
   RegisterCodec(p.Get(), err);
   ERROR_CHECK(err);
exit:;
}

