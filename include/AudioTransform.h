/*
 Copyright (C) 2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef audio_transform_h_
#define audio_transform_h_

#include <stddef.h>

#include <common/error.h>

#include "AudioSource.h"

#include <vector>
#include <memory>

namespace audio {

struct Transform
{
   virtual void TransformAudioPacket(void *&buf, size_t &len, error *err) = 0;
   virtual ~Transform() {}
};

// Note the resampler can only handle PcmShort format.
//
Transform*
CreateResampler(
   Metadata &md,
   int newSampleRate,
   error *err
);

Transform*
CreateFormatConversion(
   Metadata &md,
   Format targetFormat,
   error *err
);

Transform*
CreateChannelMapTransform(
   Format format,
   const ChannelInfo *sourceChannels,
   int nSourceChannels,
   const ChannelInfo *targetChannels,
   int nTargetChannels,
   error *err
);

//
// Convenience methods for maintaining a stack of transforms.
//

struct AudioTransformStack
{
   std::vector<std::unique_ptr<Transform>> transforms;

   inline void
   Clear()
   {
      transforms.clear();
   }

   inline void
   TransformAudioPacket(void *&buf, size_t &len, error *err)
   {
      for (auto &trans : transforms)
      {
         trans->TransformAudioPacket(buf, len, err);
         ERROR_CHECK(err);
      }
   exit:;
   }

   inline void
   AddResampler(Metadata &md, int newSampleRate, error *err)
   {
      std::unique_ptr<Transform> trans(CreateResampler(md, newSampleRate, err));
      ERROR_CHECK(err);

      try
      {
         transforms.push_back(std::move(trans));
      }
      catch (const std::bad_alloc &)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   inline void
   AddFormatConversion(Metadata &md, Format targetFormat, error *err)
   {
      std::unique_ptr<Transform> trans(CreateFormatConversion(md, targetFormat, err));
      ERROR_CHECK(err);
      try
      {
         transforms.push_back(std::move(trans));
      }
      catch (const std::bad_alloc &)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   inline void
   AddChannelMapTransform(
      Format format,
      const ChannelInfo *sourceChannels,
      int nSourceChannels,
      const ChannelInfo *targetChannels,
      int nTargetChannels,
      error *err
   )
   {
      std::unique_ptr<Transform> trans(CreateChannelMapTransform(
         format,
         sourceChannels, nSourceChannels,
         targetChannels, nTargetChannels,
         err
      ));
      ERROR_CHECK(err);
      try
      {
         transforms.push_back(std::move(trans));
      }
      catch (const std::bad_alloc &)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   inline void
   AddChannelMapTransform(
      Metadata &md,
      const ChannelInfo *targetChannels,
      int nTargetChannels,
      error *err
   )
   {
      if (md.ChannelMap.get())
      {
         AddChannelMapTransform(
            md.Format,
            md.ChannelMap->data(), md.ChannelMap->size(),
            targetChannels, nTargetChannels,
            err
         );
      }
   }
};

} // namespace

#endif