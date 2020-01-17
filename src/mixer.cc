/*
 Copyright (C) 2020 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioDevice.h>
#include "stackarray.h"

//
// Unit conversions for volume.
//

void
audio::Mixer::GetRange(int idx, value_t &min, value_t &max, error *err)
{
   min = 0;
   max = 1000;
}

void
audio::Mixer::SetValue(int idx, const value_t *val, int n, error *err)
{
   StackArray<float, 2> floats;
   value_t min, max;
   float mult;

   GetRange(idx, min, max, err);
   ERROR_CHECK(err);

   if (min == max)
      ERROR_SET(err, unknown, "Invalid range");

   mult = 1.0f / (max - min);

   floats.resize(n, err);
   ERROR_CHECK(err);

   for (auto &f : floats)
   {
      f = mult * (*val++ - min);
   }

   SetValue(idx, floats.begin(), floats.end() - floats.begin(), err);
   ERROR_CHECK(err);
exit:;
}

int
audio::Mixer::GetValue(int idx, value_t *value, int n, error *err)
{
   int r = 0;
   StackArray<float, 2> floats;
   value_t min, max;
   GetRange(idx, min, max, err);
   ERROR_CHECK(err);

   floats.resize(n, err);
   ERROR_CHECK(err);

   r = GetValue(idx, floats.begin(), n, err);
   ERROR_CHECK(err);
   floats.resize(r, err);
   ERROR_CHECK(err);

   for (auto f : floats)
   {
      *value++ = ((max - min) * f) + min;
   }
exit:
   return r;
}

void
audio::Mixer::SetValue(int idx, const float *floats, int n, error *err)
{
   StackArray<value_t, 2> values;
   value_t min, max;
   GetRange(idx, min, max, err);
   ERROR_CHECK(err);

   values.resize(n, err);
   ERROR_CHECK(err);

   for (auto &v : values)
   {
      v = ((max - min) * (*floats++)) + min;
   }

   SetValue(idx, values.begin(), values.end() - values.begin(), err);
   ERROR_CHECK(err);
exit:;
}

int
audio::Mixer::GetValue(int idx, float *floats, int n, error *err)
{
   int r = 0;
   StackArray<value_t, 2> values;
   value_t min, max;
   float mult;

   GetRange(idx, min, max, err);
   ERROR_CHECK(err);

   if (min == max)
      ERROR_SET(err, unknown, "Invalid range");

   mult = 1.0f / (max - min);

   values.resize(n, err);
   ERROR_CHECK(err);

   r = GetValue(idx, values.begin(), n, err);
   ERROR_CHECK(err);
   values.resize(r, err);
   ERROR_CHECK(err);

   for (auto v : values)
   {
      *floats++ = mult * (v - min);
   }
exit:
   return r;
}
