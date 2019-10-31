/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioTags.h>
#include <string.h>

using namespace audio;

#define FOREACH_STRING(FN)  \
   FN(Title);               \
   FN(Subtitle);            \
   FN(ContentGroup);        \
   FN(Artist);              \
   FN(Accompaniment);       \
   FN(Composer);            \
   FN(Conductor);           \
   FN(Album);               \
   FN(Genre);               \
   FN(Publisher);           \
   FN(Isrc);                \

#define FOREACH_INTEGER(FN) \
   FN(Duration);            \
   FN(Track);               \
   FN(TrackCount);          \
   FN(Disc);                \
   FN(DiscCount);           \
   FN(Year);                \
   FN(OriginalYear);

#define FOREACH_BINARY(FN)  \
   FN(Image);               \

#define MAP(X)       \
   case X: return #X
#define PARSE(X)     \
   do { if (!strcmp(str, #X)) { md = X; return true; } } while(0)

const char *
audio::ToString(StringMetadata value)
{
   switch (value)
   {
   FOREACH_STRING(MAP)
   }
   return "<invalid>";
}

const char *
audio::ToString(IntegerMetadata value)
{
   switch (value)
   {
   FOREACH_INTEGER(MAP)
   }
   return "<invalid>";
}

const char *
audio::ToString(BinaryMetadata value)
{
   switch (value)
   {
   FOREACH_BINARY(MAP)
   }
   return "<invalid>";
}

bool
audio::TryParse(const char *str, StringMetadata &md)
{
   FOREACH_STRING(PARSE)
   return false;
}

bool
audio::TryParse(const char *str, IntegerMetadata &md)
{
   FOREACH_INTEGER(PARSE)
   return false;
}

bool
audio::TryParse(const char *str, BinaryMetadata &md)
{
   FOREACH_BINARY(PARSE)
   return false;
}