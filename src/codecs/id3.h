/*
 Copyright (C) 2017 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef id3_h_
#define id3_h_

#include <common/misc.h>
#include <string.h>

namespace audio
{

#pragma pack(1)
struct Id3Header
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
      const unsigned char *p = Size;
      uint32_t r = 0;
      while (p < Size + ARRAY_SIZE(Size))
      {
         r <<= 7;
         r |= 0x7f & *p++;
      }
      return r;
   }
};
#pragma pack()

} // end namespace

#endif
