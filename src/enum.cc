/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioTags.h>

using namespace audio;

#define MAP(X) case X: return #X

const char *
audio::ToString(StringMetadata value)
{
   switch (value)
   {
   MAP(Title);
   MAP(Subtitle);
   MAP(ContentGroup);
   MAP(Artist);
   MAP(Accompaniment);
   MAP(Composer);
   MAP(Conductor);
   MAP(Album);
   MAP(Genre);
   MAP(Publisher);
   MAP(Isrc);
   }
   return "<invalid>";
}

const char *
audio::ToString(IntegerMetadata value)
{
   switch (value)
   {
   MAP(Duration);
   MAP(Track);
   MAP(TrackCount);
   MAP(Disc);
   MAP(DiscCount);
   MAP(Year);
   MAP(OriginalYear);
   }
   return "<invalid>";
}

const char *
audio::ToString(BinaryMetadata value)
{
   switch (value)
   {
   MAP(Image);
   }
   return "<invalid>";
}