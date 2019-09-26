/*
 Copyright (C) 2017, 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef id3_h_
#define id3_h_

#include <common/misc.h>
#include <string.h>

namespace audio { namespace id3 {

enum Encoding
{
   Latin1    = 0,
   Utf16_BOM = 1, // BOM absent = LE?
   Utf16_BE  = 2,
   Utf8      = 3,
};

uint32_t
ParseSyncSafe(const unsigned char p[4]);

#pragma pack(1)
struct Header
{
   char Magic[3];
   unsigned char MajorVersion;
   unsigned char MinorVersion;
   unsigned char Flags;
   unsigned char Size[4];

   bool HasMagic() const
   {
      return !strncmp(Magic, "ID3", 3);
   }

   uint32_t ReadSize() const
   {
      return ParseSyncSafe(Size);
   }
};

struct ExtendedHeaderPrefix
{
   unsigned char Size[4];
   unsigned char FlagBytes;
};

#ifndef C99_VLA
#if defined(_MSC_VER)
#define C99_VLA 1
#else
#define C99_VLA
#endif
#endif

struct ExtendedHeaderPayload
{
   unsigned char Length;
   unsigned char Bytes[C99_VLA];
};

struct FrameHeader
{
   unsigned char Id[4];
   unsigned char Size[4];
   unsigned char Flags[2];
};

struct LegacyFrameHeader
{
   unsigned char Id[3];
   unsigned char Size[3];
};

struct LegacyImageHeader
{
   unsigned char Encoding;
   unsigned char Format[3];
   unsigned char PictureType;
};

#pragma pack()

class Parser
{
   Header header;
   MetadataReceiver *recv;

   void
   OnExtendedHeaderBit(int bitno, ExtendedHeaderPayload *payload, error *err);

   void
   OnFrame(FrameHeader *header, uint32_t frameSize, bool unsync, common::Stream *file, error *err);

public:
   Parser() : recv(nullptr) {}

   bool
   InitialParse(const void *buf, int len, error *err);

   inline
   uint32_t TagLength() const
   {
      return header.ReadSize() + 10;
   }

   void
   TryParse(common::Stream *file, MetadataReceiver *recv, error *err);
};

} } // end namespace

#endif
