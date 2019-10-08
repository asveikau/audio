/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef audiotag_h_
#define audiotag_h_

#include <common/c++/stream.h>

#include <functional>
#include <stdint.h>

namespace audio {

enum StringMetadata
{
   Title,
   Subtitle,
   ContentGroup,
   Artist,
   Accompaniment,
   Conductor,
   Album,
   Genre,
   Publisher,
};

const char *
ToString(StringMetadata);

enum IntegerMetadata
{
   Duration,
   Track,
   TrackCount,    // NB: needs to follow Track
   Disc,
   DiscCount,     // NB: needs to follow Disc
   Year,
   OriginalYear,
};

const char *
ToString(IntegerMetadata);

enum BinaryMetadata
{
   Image,
};

const char *
ToString(BinaryMetadata);

struct MetadataReceiver
{
   std::function<void(
      StringMetadata type,
      const std::function<void(std::string &str, error *err)> &parse,
      error *err
   )> OnString;

   std::function<void(
      IntegerMetadata type,
      const std::function<void(int64_t &i, error *err)> &parse,
      error *err
   )> OnInteger;

   std::function<void(
      BinaryMetadata type,
      const std::function<void(common::Stream **stream, error *err)> &parse,
      error *err
   )> OnBinaryData;

   std::function<void(
      BinaryMetadata type,
      const std::function<void(std::string &url, error *err)> &parse,
      error *err
   )> OnRemoteBinaryData;
};

} // end namespace

#endif
