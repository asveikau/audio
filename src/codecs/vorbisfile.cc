/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include <common/misc.h>
#include <common/size.h>

using namespace common;
using namespace audio;

#include <string.h>
#include <errno.h>

#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

namespace {

extern ov_callbacks callbacks;

class VorbisFile : public Source
{
   Pointer<Stream> stream;
   OggVorbis_File file;
public:
   VorbisFile(Stream *stream_) :
      stream(stream_)
   {
      memset(&file, 0, sizeof(file));
   }

   ~VorbisFile()
   {
      ov_clear(&file);
   }

   const char *Describe(void) { return "[vorbisfile]"; }

   void
   Initialize(MetadataReceiver *recv, error *err)
   {
      auto r = ov_open_callbacks(stream.Get(), &file, nullptr, 0, callbacks);
      if (r)
         ERROR_SET(err, vorbis, r);
      if (recv)
      {
         auto vc = ov_comment(&file, -1);
         if (vc)
         {
            OnOggComments(
               recv,
               vc->user_comments,
               vc->comment_lengths,
               vc->comments,
               vc->vendor,
               err
            );
            ERROR_CHECK(err);
         }
      }
   exit:;
   }

   void
   GetMetadata(Metadata *res, error *err)
   {
      auto p = ov_info(&file, -1);
      if (!p)
         ERROR_SET(err, unknown, "ov_info returned null");

      res->Format = PcmShort;
      res->SampleRate = p->rate;
      res->Channels = p->channels;
      res->SamplesPerFrame = 0;

   exit:;
   }

   int
   Read(void *buf, int len, error *err)
   {
      int endian = 1;
      int bitstream;
      long r = ov_read(
         &file,
         (char*)buf,
         len,
         !*((char*)&endian),
         2,
         1,
         &bitstream
      );
      if (r < 0)
         ERROR_SET(err, vorbis, r);
   exit:
      return r;
   }

   void Seek(uint64_t pos, error *err)
   {
      if (ov_time_seek(&file, pos / 10000000.0))
         ERROR_SET(err, unknown, "failed to seek");
   exit:;
   }

   uint64_t
   GetPosition(error *err)
   {
      return (uint64_t)(ov_time_tell(&file) * 10000000.0);
   }

   uint64_t
   GetDuration(error *err)
   {
      return (uint64_t)(ov_time_total(&file, -1) * 10000000.0);
   }

   void GetStreamInfo(audio::StreamInfo *info, error *err)
   {
      stream->GetStreamInfo(&info->FileStreamInfo, err);
      ERROR_CHECK(err);

      Source::GetStreamInfo(info, err);
      ERROR_CHECK(err);
   exit:;
   }

private:

   void
   error_set_vorbis(error *err, long code)
   {
      const char *msg = "Vorbis error";

      switch (code)
      {
      case OV_EOF:
         msg = "End of file";
         break;
      case OV_HOLE:
         msg = "Gap in page sequence numbers";
         break;
      case OV_EREAD:
         msg = "Read error";
         break; 
      case OV_EFAULT:
         msg = "Bad pointer or internal error";
         break;
      case OV_EIMPL:
         msg = "Not implemented";
         break;
      case OV_ENOTVORBIS:
         msg = "Not a vorbis file";
         break;
      case OV_EBADHEADER:
         msg = "Invalid header";
         break;
      case OV_EVERSION:
         msg = "Unrecognized version";
         break;
      case OV_ENOTAUDIO:
         msg = "Not audio";
         break;
      case OV_EBADPACKET:
         msg = "Bad packet";
         break;
      case OV_EBADLINK:
         msg = "Bad link";
         break;
      case OV_ENOSEEK:
         msg = "Stream not seekable";
         break;
      }

      error_set_unknown(err, msg);
   }
};

size_t
VorbisRead(
   void *buf,
   size_t size,
   size_t nmemb,
   void *streamp
)
{
   Stream *stream = (Stream*)streamp;
   error err;
   size_t r = 0;
   size_t input;

   if (size_mult(size, nmemb, &input))
      ERROR_SET(&err, unknown, "Integer overflow");

   r = stream->Read(buf, input, &err);
   ERROR_CHECK(&err);

exit:
   if (ERROR_FAILED(&err)) r = 0;
   return r;
}

int
VorbisSeek(
   void *streamp,
   int64_t offset,
   int whence
)
{
   Stream *stream = (Stream*)streamp;
   error err;
   int r = 0;
   stream->Seek(offset, whence, &err);
   ERROR_CHECK(&err);
exit:
   if (ERROR_FAILED(&err)) r = -1;
   return r;
}

long
VorbisTell(void *streamp)
{
   Stream *stream = (Stream*)streamp;
   error err;
   long r = 0;
   r = stream->GetPosition(&err);
   ERROR_CHECK(&err);
exit:
   if (ERROR_FAILED(&err)) r = -1;
   return r;
}

ov_callbacks callbacks =
{
   VorbisRead,
   VorbisSeek,
   NULL,
   VorbisTell,
};

} // end namespace

void audio::CreateVorbisSource(
   common::Stream *file,
   CodecArgs &params,
   Source **obj,
   error *err
)
{
   Pointer<VorbisFile> r;
   try
   {
      *r.GetAddressOf() = new VorbisFile(file);
   }
   catch (const std::bad_alloc&)
   {
      ERROR_SET(err, nomem);
   }
   r->Initialize(params.Metadata, err);
exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *obj = r.Detach();;
}
