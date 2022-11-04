/*
 Copyright (C) 2017, 2019, 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef audiosource_h_
#define audiosource_h_

#include <common/c++/refcount.h>
#include <common/c++/stream.h>
#include <functional>

namespace audio {

using common::RefCountable;

enum Format
{
   PcmShort, // 16-bit signed, native byte order
   Pcm24,    // 24-bit signed, native byte order
   Pcm24Pad, // 24-bit signed, native byte order, expressed as 32 bits
   PcmFloat, // 32-bit float, native byte order
};

static inline int
GetBitsPerSample(Format fmt)
{
   switch (fmt)
   {
   case PcmShort:
      return 16;
   case Pcm24:
      return 24;
   case Pcm24Pad:
      return 32;
   case PcmFloat:
      return 32;
   default:
      return -1;
   }
}

static inline const char *
GetFormatName(Format fmt)
{
   switch (fmt)
   {
   case PcmShort:
      return "s16ne";
   case Pcm24:
      return "s24ne";
   case Pcm24Pad:
      return "s24ne-32";
   case PcmFloat:
      return "float32";
   default:
      return "Invalid format";
   }
}

struct Metadata
{
   int SampleRate;
   int Channels;
   int SamplesPerFrame; // "frame" in the mp3 sense; a sensible packet size.
   Format Format;
};

struct StreamInfo
{
   common::StreamInfo FileStreamInfo;
   bool ContainerHasSlowSeek;
   bool DurationKnown;

   StreamInfo() : ContainerHasSlowSeek(false), DurationKnown(true) {}
};

struct Source : public RefCountable
{
   Source() : MetadataChanged(false), ContainerHasSlowSeek(false) {}

   // Read() may change this to true, indicating you need to
   // GetMetadata() again and possibly re-initialize the device.
   // This would be kind of a "freak event" for corner cases of various
   // container formats.
   //
   bool MetadataChanged;

   virtual void GetStreamInfo(StreamInfo *info, error *err);

   // A short, programmer-ese string to describe the audio format.
   //
   virtual const char *Describe(void) { return nullptr; }

   // Query sample rate, format, etc.
   //
   virtual void GetMetadata(Metadata *res, error *err) = 0;

   // Read samples.
   // @len: length in bytes
   //
   virtual int Read(void *buf, int len, error *err) = 0;

   // @pos: time in 100ns units
   //
   virtual void Seek(uint64_t pos, error *err) = 0;

   // Get stream duration in 100ns units.
   // TBD: what to do for streaming formats.  Currently, we block until
   // a size can be known.
   //
   virtual uint64_t GetDuration(error *err) = 0;

   // Current playback position (i.e. how much as been through Read())
   // in 100ns units.
   //
   virtual uint64_t GetPosition(error *err) = 0;

protected:
   // Set by a subclass to indicate that the format has slow seeking.
   // (eg. ADTS)
   //
   bool ContainerHasSlowSeek;
};

} // namespace
#endif
