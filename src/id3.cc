/*
 Copyright (C) 2017, 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include "id3.h"

#include <common/c++/new.h>
#include <common/misc.h>
#include <common/utf.h>

#include <map>
#include <string>
#include <vector>

namespace {

enum FrameDataType
{
   String,
   Integer,
   Binary,
};

struct
FrameMapping
{
   FrameDataType Type;
   int Enum;
   char Id[5];
   char LegacyId[4];
};

const FrameMapping
Mappings[] =
{
   {String,  audio::Title,         "TIT2", "TT2"},
   {String,  audio::Subtitle,      "TIT3", "TT3"},
   {String,  audio::ContentGroup,  "TIT1", "TT1"},
   {String,  audio::Artist,        "TPE1", "TP1"},
   {String,  audio::Accompaniment, "TPE2", "TP2"},
   {String,  audio::Composer,      "TCOM", "TCM"},
   {String,  audio::Conductor,     "TPE3", "TP3"},
   {String,  audio::Album,         "TALB", "TAL"},
   {String,  audio::Genre,         "TCON", "TCO"},
   {String,  audio::Publisher,     "TPUB", "TPB"},
   {String,  audio::Isrc,          "TSRC", "TRC"},

   {Integer, audio::Duration,      "TLEN", "TLE"},
   {Integer, audio::Track,         "TRCK", "TRK"},
   {Integer, audio::Disc,          "TPOS", "TPA"},
   {Integer, audio::Year,          "TYER", "TYE"},
   {Integer, audio::OriginalYear,  "TORY", ""   },

   {Binary,  audio::Image,         "APIC", "PIC"},
};

bool
is_zero(const void *bufp, int n)
{
   const unsigned char *buf = (const unsigned char*)bufp;
   while (n--)
   {
      if (*buf++)
         return false;
   }
   return true;
}

uint32_t
ParseWord(const unsigned char Size[4])
{
   const unsigned char *p = Size;
   uint32_t r = 0;
   while (p < Size + 4)
   {
      r <<= 8;
      r |= *p++;
   }
   return r;
}

} // end namespace

uint32_t
audio::id3::ParseSyncSafe(const unsigned char Size[4])
{
   const unsigned char *p = Size;
   uint32_t r = 0;
   while (p < Size + 4)
   {
      r <<= 7;
      r |= 0x7f & *p++;
   }
   return r;
}

bool
audio::id3::Parser::InitialParse(const void *buf, int len, error *err)
{
   bool r = false;
   auto p = (const Header*)buf;

   if (len >= sizeof(*p) && p->HasMagic())
   {
      header = *p;
      r = true;
   }

   return r;
}

void
audio::id3::Parser::OnExtendedHeaderBit(int bitno, ExtendedHeaderPayload *payload, error *err)
{
   //
   // These aren't very interesting.  So do nothing.
   //
}

namespace {

class UnsyncStreamWrapper : public common::PStream
{
   common::Pointer<common::Stream> baseStream;
   uint64_t offset;
   uint32_t length;
   std::vector<uint32_t> removalOffsets;

public:

   void
   Initialize(common::Stream *file, uint32_t length, error *err)
   {
      bool saw_1st = false;
      uint32_t ch = 0;

      baseStream = file;
      offset = file->GetPosition(err);
      ERROR_CHECK(err);

      this->length = length;

      while (length)
      {
         unsigned char buf[4096];
         size_t r = 0;

         r = file->Read(buf, MIN(sizeof(buf), length), err);
         ERROR_CHECK(err);

         if (!r)
            break;

         length -= r;

         for (size_t i=0; i<r; ++i)
         {
            auto b = buf[i];
            if (!saw_1st)
            {
               if (b == 0xff)
               {
                  saw_1st = true;
               }
            }
            else
            {
               if (!b)
               {
                  try
                  {
                     removalOffsets.push_back(ch);
                  }
                  catch (std::bad_alloc)
                  {
                     ERROR_SET(err, nomem);
                  }
               }
               if (b != 0xff)
                  saw_1st = false;
            }
            ++ch;
         }
      }

   exit:;
   }

   size_t
   Read(void *buf, size_t len, uint64_t pos, error *err)
   {
      int idx = 0;
      size_t r = 0;

      for (auto off : removalOffsets)
      {
         if (off <= pos)
         {
            ++idx;
            ++pos;
         }
         else
         {
            break;
         }
      }

      while (len)
      {
         size_t len2 = MIN(len, idx < removalOffsets.size() ? removalOffsets[idx] - pos : len);
         size_t r2 = 0;

         baseStream->Seek(offset + pos, SEEK_SET, err);
         ERROR_CHECK(err);

         r2 = baseStream->Read(buf, MIN(len2, length - pos), err);
         ERROR_CHECK(err);

         r += r2;

         if (r2 < len2)
            break;

         len -= r2;
         buf = (char*)buf + r2;
         pos += r2;

         while (idx < removalOffsets.size() &&
                pos == removalOffsets[idx])
         {
            ++idx;
            ++pos;
         }
      }

   exit:
      return r;
   }

   uint64_t GetSize(error *err)
   {
      return length - removalOffsets.size();
   }

   void
   GetStreamInfo(common::StreamInfo *info, error *err)
   {
      baseStream->GetStreamInfo(info, err);
   }
};

template<typename Read>
bool
ParseLatin1(Read read, uint32_t *r, error *err)
{
   bool parsed = false;
   unsigned char ch = 0;
   int ret = read(&ch, 1, err);
   ERROR_CHECK(err);
   if (ret != 1)
      goto exit;
   *r = ch;
   parsed = true;
exit:
   return parsed;
}

uint16_t
DecodeLittleEndian(unsigned char p[2])
{
   return p[0] | (((uint16_t)p[1]) << 8);
}

uint16_t
DecodeBigEndian(unsigned char p[2])
{
   return p[1] | (((uint16_t)p[0]) << 8);
}

template<typename Decode, typename Read>
bool
ParseUtf16(Read read, Decode decode, uint32_t *r, error *err)
{
   bool parsed = false;
   unsigned char buf[4];
   uint16_t buf16[2];
   const uint16_t *p = buf16;
   int ret = read(buf, 2, err);
   ERROR_CHECK(err);
   if (ret != 2)
      goto exit;
   buf16[0] = decode(buf);
   if (buf16[0] >= 0xd800 && buf16[0] < 0xe000)
   {
      ret = read(buf+2, 2, err);
      if (ret != 2)
         goto exit;
      buf16[1] = decode(buf+2);
   }
   *r = utf16_decode(&p);
   parsed = (*r != -1);
exit:
   return parsed;
}

template<typename Parse>
void
Transcode(Parse parse, std::vector<char> &buf, error *err)
{
   uint32_t u;
   while (parse(&u, err))
   {
      char utf8buf[8];
      int r = utf8_encode(u, utf8buf, sizeof(utf8buf));
      if (r < 0)
         ERROR_SET(err, unknown, "utf8_encode failed");
      try
      {
         buf.insert(buf.end(), utf8buf, utf8buf + r);
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
   }
   ERROR_CHECK(err);
exit:;
}

bool
TryParseBom(common::Stream *file, uint32_t &size, unsigned char bom[2], error *err)
{
   bool sawBom = false;
   int r = 0;

   if (size < 2)
      goto exit;

   r = file->Read(bom, 2, err);
   ERROR_CHECK(err);
   if (r != 2)
      ERROR_SET(err, unknown, "Short read");

   if (!memcmp(bom, "\xff\xfe", 2) || !memcmp(bom, "\xfe\xff", 2))
   {
      sawBom = true;
      size -= 2;
      goto exit;
   }

   // No BOM.  Forget we even checked.
   //
   file->Seek(-2, SEEK_CUR, err);
   ERROR_CHECK(err);

exit:
   return sawBom;
}

template<typename Reader>
void
GetStringParser(
   Reader reader,
   common::Stream *file,
   uint32_t &frameSize,
   audio::id3::Encoding encoding,
   std::function<void(std::vector<char> &, error *)> &parse,
   error *err
)
{
   unsigned char bom[2] = {0};
   auto innerParse = parse;

#define WRAP_TRANSCODE(expr)                                        \
   [reader] (std::vector<char> &r, error *err) -> void              \
   {                                                                \
      Transcode(                                                    \
         [reader] (uint32_t *r, error *err) -> bool {return expr;}, \
         r,                                                         \
         err                                                        \
      );                                                            \
   }

   switch (encoding)
   {
   case audio::id3::Utf8:
      parse = [reader, frameSize] (std::vector<char> &r, error *err) -> void
      {
         try
         {
            r.resize(frameSize);
         }
         catch (std::bad_alloc)
         {
            ERROR_SET(err, nomem);
         }

         r.resize(reader(r.data(), frameSize, err));
         ERROR_CHECK(err);
      exit:;
      };
      break;
   case audio::id3::Latin1:
      parse = WRAP_TRANSCODE(ParseLatin1(reader, r, err));
      break;
   case audio::id3::Utf16_BOM:
      if (TryParseBom(file, frameSize, bom, err))
      {
      gotBom:
         if (bom[0] == 0xff)
            goto littleEndian;
         else
            goto bigEndian;
      }
      ERROR_CHECK(err);
   littleEndian:
      parse = WRAP_TRANSCODE(ParseUtf16(reader, DecodeLittleEndian, r, err));
      break;
   case audio::id3::Utf16_BE:
      if (TryParseBom(file, frameSize, bom, err))
         goto gotBom;
      ERROR_CHECK(err);
   bigEndian:
      parse = WRAP_TRANSCODE(ParseUtf16(reader, DecodeBigEndian, r, err));
      break;
   default:
      ERROR_SET(err, unknown, "unrecognized encoding");
   }

   innerParse = parse;
   parse = [innerParse] (std::vector<char> &vec, error *err) -> void
   {
      auto oldSize = vec.size();
      innerParse(vec, err);
      if (!ERROR_FAILED(err) &&
          (vec.size() == oldSize || vec[vec.size()-1]))
      {
         try
         {
            vec.push_back(0);
         }
         catch (std::bad_alloc)
         {
            error_set_nomem(err);
         }
      }
   };
exit:;
}

template<typename Reader>
void
GetStringParserNullTerminator(
   Reader reader,
   common::Stream *file,
   uint32_t &frameSize,
   audio::id3::Encoding encoding,
   std::function<void(std::vector<char> &, error *)> &parse,
   error *err
)
{
   GetStringParser(
      [reader, encoding] (void *buf, int len, error *err) -> int
      {
         int r = 0;
         if (!len)
            goto exit;
         if (encoding == audio::id3::Utf8)
         {
            // The parser will request in large blocks, so split it up.
            // to check for a terminator.
            //
            while (len)
            {
               int r2 = reader(buf, 1, err);
               if (ERROR_FAILED(err) || !r2)
                  goto exit;
               len--;
               r++;
               if (!*(const char*)buf)
                  goto exit;
               buf = (char*)buf+1;
            }
         }
         else
         {
            r = reader(buf, len, err);
            if (!ERROR_FAILED(err) && r == len && is_zero(buf, r))
               r = 0;
         }
      exit:
         return r;
      },
      file,
      frameSize,
      encoding,
      parse,
      err
   );
}

} // end namespace

void
audio::id3::Parser::OnFrame(FrameHeader *header, uint32_t frameSize, bool unsync, common::Stream *file, error *err)
{
   common::Pointer<common::Stream> altStream;
   int r = 0;

   bool legacy = this->header.MajorVersion < 3;
   auto mapping = Mappings;

   for (; mapping < Mappings + ARRAY_SIZE(Mappings); ++mapping)
   {
      auto id = legacy ? mapping->LegacyId : mapping->Id;
      auto idLen = legacy ? 3 : 4;

      if (!*id)
         continue;

      if (!memcmp(header->Id, id, idLen))
      {
         break;
      }
   }
   if (mapping >= Mappings + ARRAY_SIZE(Mappings))
      return;

   if (unsync)
   {
      common::Pointer<UnsyncStreamWrapper> wrapper;

      common::New(wrapper.GetAddressOf(), err);
      ERROR_CHECK(err);

      wrapper->Initialize(file, frameSize, err);
      ERROR_CHECK(err);

      wrapper->ToStream(altStream.GetAddressOf(), err);
      ERROR_CHECK(err);
      file = altStream.Get();

      frameSize = file->GetSize(err);
      ERROR_CHECK(err);
   }

   if (header->Id[0] == 'T')
   {
      unsigned char encoding = 0;
      std::function<void(std::vector<char> &, error *)> parse;

      if (!frameSize)
         ERROR_SET(err, unknown, "Could not read encoding");
      --frameSize;
      r = file->Read(&encoding, sizeof(encoding), err);
      ERROR_CHECK(err);
      if (r != sizeof(encoding))
         ERROR_SET(err, unknown, "Could not read encoding");

      auto reader =
         [file, &frameSize] (void *buf, int len, error *err) -> int
         {
            len = MIN(frameSize, len);
            int r = 0;
            if (len)
               r = file->Read(buf, len, err);
            if (r > 0)
               frameSize -= r;
            return r;
         };

      GetStringParser(
         reader,
         file,
         frameSize,
         (Encoding)encoding,
         parse,
         err
      );
      ERROR_CHECK(err);

      switch (mapping->Type)
      {
      case String:
         if (recv && recv->OnString)
         {
            recv->OnString(
               (StringMetadata)mapping->Enum,
               [parse, mapping] (std::string &str, error *err) -> void
               {
                  std::vector<char> vec;
                  parse(vec, err);
                  ERROR_CHECK(err);
                  try
                  {
                     str = std::string(vec.data(), vec.size() ? vec.size() - 1 : 0);
                     if (((StringMetadata)mapping->Enum) == Genre)
                     {
                        auto p = TryParseGenre(str);
                        if (p)
                           str = p;
                     }
                  }
                  catch (std::bad_alloc)
                  {
                     ERROR_SET(err, nomem);
                  }
               exit:;
               },
               err
            );
            ERROR_CHECK(err);
         }
         break;
      case Integer:
         if (recv && recv->OnInteger)
         {
            switch (mapping->Enum)
            {
            case (int)Track:
            case (int)Disc:
               {
                  std::vector<char> vec;
                  char *p = nullptr;

                  parse(vec, err);
                  ERROR_CHECK(err);

                  if ((p = strchr(vec.data(), '/')))
                  {
                     auto q = &(*p = 0) + 1;
                     auto total = strtoll(q, &p, 10);
                     recv->OnInteger(
                        (IntegerMetadata)(mapping->Enum + 1),
                        [total] (int64_t &i, error *err) -> void { i = total; },
                        err
                     );
                     ERROR_CHECK(err);
                  }
                  auto n = strtoll(vec.data(), &p, 10);
                  recv->OnInteger(
                     (IntegerMetadata)mapping->Enum,
                     [n] (int64_t &i, error *err) -> void { i = n; },
                     err
                  );
                  ERROR_CHECK(err);
                  goto exit;
               }
               break;
            }

            recv->OnInteger(
               (IntegerMetadata)mapping->Enum,
               [parse, mapping] (int64_t &i, error *err) -> void
               {
                  std::vector<char> vec;
                  char *p = nullptr;

                  parse(vec, err);
                  ERROR_CHECK(err);
                  i = strtoll(vec.data(), &p, 10);

                  // special cases.
                  //
                  if (mapping->Enum == (int)Duration)
                  {
                     i *= 10000000LL / 1000;
                  }
               exit:;
               },
               err
            );
            ERROR_CHECK(err);
         }
         break;
      default:;
      }

      goto exit;
   }

   if (mapping->Type == Binary && mapping->Enum == (int)Image)
   {
      char legacyFormat[sizeof(LegacyImageHeader::Format) + 1] = {0};
      std::vector<char> buf;
      const char *format = nullptr;
      const char *desc = nullptr;
      unsigned char encoding = 0;
      unsigned char type = 0;
      int descOffset = -1;
      int formatOffset = -1;
      std::function<void(std::vector<char> &, error *)> generate;

      // XXX dupplication
      auto reader =
         [file, &frameSize] (void *buf, int len, error *err) -> int
         {
            len = MIN(frameSize, len);
            int r = 0;
            if (len)
               r = file->Read(buf, len, err);
            if (r > 0)
               frameSize -= r;
            return r;
         };

      if (legacy)
      {
         LegacyImageHeader legacyImage;
         if (frameSize < sizeof(legacyImage))
            ERROR_SET(err, unknown, "Short read");
         int r = file->Read(&legacyImage, sizeof(legacyImage), err);
         ERROR_CHECK(err);
         if (r != sizeof(legacyImage))
            ERROR_SET(err, unknown, "Short read");
         frameSize -= sizeof(legacyImage);
         encoding = legacyImage.Encoding;
         memcpy(legacyFormat, legacyImage.Format, sizeof(legacyImage.Format));
         format = legacyFormat;
         type = legacyImage.PictureType;
      }
      else
      {
         if (!frameSize)
            ERROR_SET(err, unknown, "Short read");
         auto r = file->Read(&encoding, 1, err);
         ERROR_CHECK(err);
         if (r != 1)
            ERROR_SET(err, unknown, "Short read");
         frameSize--;

         GetStringParserNullTerminator(
            reader,
            file,
            frameSize,
            Encoding::Utf8,
            generate,
            err
         );
         ERROR_CHECK(err);

         formatOffset = buf.size();
         generate(buf, err);
         ERROR_CHECK(err);

         if (!frameSize)
            ERROR_SET(err, unknown, "Short read");
         r = file->Read(&type, 1, err);
         ERROR_CHECK(err);
         if (r != 1)
            ERROR_SET(err, unknown, "Short read");
         frameSize--;
      }

      descOffset = buf.size();

      GetStringParserNullTerminator(
         reader,
         file,
         frameSize,
         (Encoding)encoding,
         generate,
         err
      );
      ERROR_CHECK(err);

      generate(buf, err);
      ERROR_CHECK(err);

      if (descOffset >= 0)
         desc = buf.data() + descOffset;

      if (formatOffset >= 0)
         format = buf.data() + formatOffset;

      if (recv)
      {
         bool remote = !strcmp(format, "-->");
         if (remote)
         {
            if (recv->OnRemoteBinaryData)
            {
               recv->OnRemoteBinaryData(
                  (BinaryMetadata)mapping->Enum,
                  [file, frameSize] (std::string &url, error *err) -> void
                  {
                     try
                     {
                        std::vector<char> buf;
                        buf.resize(frameSize + 1);
                        int r = file->Read(buf.data(), frameSize, err);
                        ERROR_CHECK(err);
                        if (r != frameSize)
                           ERROR_SET(err, unknown, "Short read");
                        url = buf.data();
                     }
                     catch (std::bad_alloc)
                     {
                        ERROR_SET(err, nomem);
                     }
                  exit:;
                  },
                  err
               );
               ERROR_CHECK(err);
            }
         }
         else if (recv->OnBinaryData)
         {
            auto pos = file->GetPosition(err);
            ERROR_CHECK(err);
            recv->OnBinaryData(
               (BinaryMetadata)mapping->Enum,
               [file, pos, frameSize] (common::Stream **stream, error *err) -> void
               {
                  file->Substream(pos, frameSize, stream, err);
               },
               err
            );
            ERROR_CHECK(err);
         }
      }
   }

exit:;
}

void
audio::id3::Parser::TryParse(common::Stream *file, MetadataReceiver *recv, error *err)
{
   FrameHeader frameHeader;
   size_t r = 0;
   uint32_t remaining = ParseSyncSafe(header.Size);
   bool globalUnsync = false;

   uint64_t origin = file->GetPosition(err);
   ERROR_CHECK(err);

   this->recv = recv;

   switch (header.MajorVersion)
   {
   case 0:
   case 1:
   case 2:
      //
      // These are covered by LegacyFrameHeader.
      //
   case 3:
      //
      // This one bumps up IDs and frame lengths to 32 bits.
      //
   case 4:
      //
      // This one makes the frame length syncsafe, adds per-frame unsych,
      // adds new string encodings.
      //
      break;
   default:
      goto exit;
   }

   globalUnsync = (header.Flags & (1<<7)) ? true : false;

   // Extended header
   //
   if ((header.Flags & (1<<6)))
   {
      ExtendedHeaderPrefix extended;
      unsigned char extendedHeaderBuf[256];
      uint32_t extendedLength;
      unsigned char *p = extendedHeaderBuf;
      unsigned char flags;

      if (remaining < sizeof(extended))
         ERROR_SET(err, unknown, "tag length exceeded");

      remaining -= sizeof(extended);

      r = file->Read(&extended, sizeof(extended), err);
      ERROR_CHECK(err);
      if (r != sizeof(extended))
         ERROR_SET(err, unknown, "Unexpected short read");

      extendedLength = ParseSyncSafe(extended.Size);

      if (extendedLength > remaining)
         ERROR_SET(err, unknown, "tag length exceeded");

      remaining -= extendedLength;

      // V3 has different extended header...
      //
      if (header.MajorVersion == 3)
      {
         unsigned char delta[5];
         uint32_t padding = 0;

         if (remaining < sizeof(delta))
            ERROR_SET(err, unknown, "Extended header: not enough space");
         remaining -= sizeof(delta);

         r = file->Read(delta, sizeof(delta), err);
         ERROR_CHECK(err);

         if (r != sizeof(delta))
            ERROR_SET(err, unknown, "Extended header: not enough space");

         extendedLength += sizeof(delta) + sizeof(extended);
         padding = ParseWord(delta+1);

         if (padding > remaining)
            ERROR_SET(err, unknown, "Padding exceeds tag length");
         remaining -= padding;

         goto skipExtended;
      }

      if (((int32_t)extended.FlagBytes) + sizeof(ExtendedHeaderPrefix) > extendedLength)
         ERROR_SET(err, unknown, "Extended header: Flag bytes exceeds parent length");

      r = file->Read(extendedHeaderBuf, extended.FlagBytes, err);
      ERROR_CHECK(err);
      if (r != extended.FlagBytes)
         ERROR_SET(err, unknown, "Unexpected short read");

      if (!extended.FlagBytes)
         ERROR_SET(err, unknown, "Unexpectedly short flag byte count");
      flags = *p++;
      --extended.FlagBytes;

      for (int i = 7; i >= 0; --i)
      {
         if ((flags) & (1<<i))
         {
            unsigned char len = 0;

            if (!extended.FlagBytes)
               ERROR_SET(err, unknown, "Unexpectedly short flag byte count");

            len = *p++;
            --extended.FlagBytes;

            if (len > extended.FlagBytes)
               ERROR_SET(err, unknown, "Extended header: Flag bytes exceeds parent length");

            OnExtendedHeaderBit(i, (ExtendedHeaderPayload*)(p - 1), err);
            ERROR_CHECK(err);

            p += len;
            extended.FlagBytes -= len;
         }
      }

   skipExtended:
      file->Seek(origin + extendedLength, SEEK_SET, err);
      ERROR_CHECK(err);
   }

   // Footer
   //
   if ((header.Flags & (1<<4)))
   {
      if (remaining > 10)
         ERROR_SET(err, unknown, "tag length exceeded");

      remaining -= 10;
   }

   while (remaining > sizeof(frameHeader))
   {
      uint32_t frameSize;
      bool unsync = globalUnsync;

      if (header.MajorVersion < 3)
      {
         LegacyFrameHeader legacy;

         r = file->Read(&legacy, sizeof(legacy), err);
         ERROR_CHECK(err);
         if (r < sizeof(legacy))
            break;

         memcpy(frameHeader.Id, legacy.Id, sizeof(legacy.Id));
         frameHeader.Id[sizeof(legacy.Id)] = 0;
         *frameHeader.Size = 0;
         memcpy(frameHeader.Size + 1, legacy.Size, sizeof(legacy.Size));
         memset(frameHeader.Flags, 0, sizeof(frameHeader.Flags));
      }
      else
      {
         r = file->Read(&frameHeader, sizeof(frameHeader), err);
         ERROR_CHECK(err);
         if (r < sizeof(frameHeader))
            break;
      }

      if (header.MajorVersion >= 4)
         unsync = (frameHeader.Flags[1] & (1<<1)) ? true : false;

      // Check for padding.
      //
      if (is_zero(frameHeader.Id, sizeof(frameHeader.Id)))
      {
         break;
      }

      remaining -= r;
      frameSize = (header.MajorVersion >= 4) ? ParseSyncSafe(frameHeader.Size) : ParseWord(frameHeader.Size);

      if (frameSize > remaining)
         ERROR_SET(err, unknown, "tag length exceeded");

      remaining -= frameSize;

      auto oldPos = file->GetPosition(err);
      ERROR_CHECK(err);

      // Don't bother to support encrypted frames.
      //
      if ((frameHeader.Flags[1] & (1<<2)))
         goto skipFrame;

      // XXX: Compression might be nice but not now.
      //
      if ((frameHeader.Flags[1] & (1<<3)))
         goto skipFrame;

      OnFrame(&frameHeader, frameSize, unsync, file, err);
      ERROR_CHECK(err);

   skipFrame:
      file->Seek(oldPos+frameSize, SEEK_SET, err);
      ERROR_CHECK(err);
   }

exit:
   this->recv = nullptr;
}

const char *
audio::id3::Parser::TryParseGenre(const std::string &str)
{
   int start = 0, len = str.length();

   if (len >= 3 &&
       str[start] == '(' &&
       str[len-1] == ')')
   {
      ++start;
      --len;
   }

   if (len <= 0)
      return nullptr;

   int r = 0;

   for (int i=start; i<len; ++i)
   {
      static const char digits[] = "0123456789";
      const char *p = strchr(digits, str[i]);
      if (!p)
         return nullptr;
      r *= 10;
      r += (p - digits);
   }

   return GetId3V1Genre(r);
}

const char *
audio::id3::GetId3V1Genre(int i)
{
   static const char *genres[] =
   {
      "Blues",
      "Classic Rock",
      "Country",
      "Dance",
      "Disco",
      "Funk",
      "Grunge",
      "Hip-Hop",
      "Jazz",
      "Metal",
      "New Age",
      "Oldies",
      "Other",
      "Pop",
      "R&B",
      "Rap",
      "Reggae",
      "Rock",
      "Techno",
      "Industrial",
      "Alternative",
      "Ska",
      "Death Metal",
      "Pranks",
      "Soundtrack",
      "Euro-Techno",
      "Ambient",
      "Trip-Hop",
      "Vocal",
      "Jazz+Funk",
      "Fusion",
      "Trance",
      "Classical",
      "Instrumental",
      "Acid",
      "House",
      "Game",
      "Sound Clip",
      "Gospel",
      "Noise",
      "AlternRock",
      "Bass",
      "Soul",
      "Punk",
      "Space",
      "Meditative",
      "Instrumental Pop",
      "Instrumental Rock",
      "Ethnic",
      "Gothic",
      "Darkwave",
      "Techno-Industrial",
      "Electronic",
      "Pop-Folk",
      "Eurodance",
      "Dream",
      "Southern Rock",
      "Comedy",
      "Cult",
      "Gangsta",
      "Top 40",
      "Christian Rap",
      "Pop/Funk",
      "Jungle",
      "Native American",
      "Cabaret",
      "New Wave",
      "Psychadelic",
      "Rave",
      "Showtunes",
      "Trailer",
      "Lo-Fi",
      "Tribal",
      "Acid Punk",
      "Acid Jazz",
      "Polka",
      "Retro",
      "Musical",
      "Rock & Roll",
      "Hard Rock",
      "Folk",
      "Folk-Rock",
      "National Folk",
      "Swing",
      "Fast Fusion",
      "Bebob",
      "Latin",
      "Revival",
      "Celtic",
      "Bluegrass",
      "Avantgarde",
      "Gothic Rock",
      "Progressive Rock",
      "Psychedelic Rock",
      "Symphonic Rock",
      "Slow Rock",
      "Big Band",
      "Chorus",
      "Easy Listening",
      "Acoustic",
      "Humour",
      "Speech",
      "Chanson",
      "Opera",
      "Chamber Music",
      "Sonata",
      "Symphony",
      "Booty Bass",
      "Primus",
      "Porn Groove",
      "Satire",
      "Slow Jam",
      "Club",
      "Tango",
      "Samba",
      "Folklore",
      "Ballad",
      "Power Ballad",
      "Rhythmic Soul",
      "Freestyle",
      "Duet",
      "Punk Rock",
      "Drum Solo",
      "A capella",
      "Euro-House",
      "Dance Hall",
   };

   if (i >= 0 && i < ARRAY_SIZE(genres))
      return genres[i];
   return nullptr;
}