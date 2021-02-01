/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>

using namespace common;
using namespace audio;

#include <string.h>
#include <errno.h>

#include <opusfile.h>

namespace {

extern const OpusFileCallbacks callbacks;

class OpusFile : public Source
{
   Pointer<Stream> stream;
   OggOpusFile *file;
public:
   OpusFile(Stream *stream_) :
      stream(stream_), file(nullptr)
   {
   }

   ~OpusFile()
   {
      if (file)
         op_free(file);
   }

   const char *Describe(void) { return "[opusfile]"; }

   void
   Initialize(MetadataReceiver *recv, error *err)
   {
      int r = 0;

      file = op_open_callbacks(stream.Get(), &callbacks, nullptr, 0, &r);
      if (!file)
         ERROR_SET(err, opusfile, r);
      if (recv)
      {
         auto tags = op_tags(file, -1);
         if (tags)
         {
            OnOggComments(
               recv,
               tags->user_comments,
               tags->comment_lengths,
               tags->comments,
               tags->vendor,
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
      res->Format = PcmShort;
      res->SampleRate = 48000;
      res->Channels = 2;
      res->SamplesPerFrame = 0;
   }

   int
   Read(void *buf, int len, error *err)
   {
      int r = 0;
      int bytesPerPacket = 2 * 2;
      r = op_read_stereo(file, (opus_int16*)buf, len/bytesPerPacket);
      if (r < 0)
         ERROR_SET(err, opusfile, r); 
      r *= bytesPerPacket;
   exit:
      return r;
   }

   void Seek(uint64_t pos, error *err)
   {
      int r = op_pcm_seek(file, pos * 48000 / 10000000LL);
      if (r)
         ERROR_SET(err, opusfile, r);
   exit:;
   }

   uint64_t
   GetPosition(error *err)
   {
      return op_pcm_tell(file) * 10000000LL / 48000;
   }

   uint64_t
   GetDuration(error *err)
   {
      return op_pcm_total(file, -1) * 10000000LL / 48000;
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
   error_set_opusfile(error *err, int code)
   {
      const char *msg = "opus error";

      switch (code)
      {
      case OP_EOF:
         msg = "End of file";
         break;
      case OP_HOLE:
         msg = "Gap in page sequence numbers";
         break;
      case OP_EREAD:
         msg = "Read error";
         break;
      case OP_EFAULT:
         msg = "Bad pointer or internal error";
         break;
      case OP_EIMPL:
         msg = "Not implemented";
         break;
      case OP_EINVAL:
         msg = "Invalid argument";
         break;
      case OP_ENOTFORMAT:
         msg = "Not an opus file";
         break;
      case OP_EBADHEADER:
         msg = "Invalid header";
         break;
      case OP_EVERSION:
         msg = "Unrecognized version";
         break;
      case OP_ENOTAUDIO:
         msg = "Not audio";
         break;
      case OP_EBADPACKET:
         msg = "Bad packet";
         break;
      case OP_EBADLINK:
         msg = "Bad link";
         break;
      case OP_ENOSEEK:
         msg = "Stream not seekable";
         break; 
      case OP_EBADTIMESTAMP:
         msg = "Bad timestamp";
         break;
      }

      error_set_unknown(err, msg);
   }
};

int
OpusRead(
   void *streamp,
   unsigned char *buf,
   int len
)
{
   Stream *stream = (Stream*)streamp;
   error err;
   int r = 0;

   r = stream->Read(buf, len, &err);
   ERROR_CHECK(&err);

exit:
   if (ERROR_FAILED(&err)) r = -1;
   return r;
}

int
OpusSeek(
   void *streamp,
   opus_int64 offset,
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

opus_int64
OpusTell(void *streamp)
{
   Stream *stream = (Stream*)streamp;
   error err;
   int64_t r = 0;
   r = stream->GetPosition(&err);
   ERROR_CHECK(&err);
exit:
   if (ERROR_FAILED(&err)) r = -1;
   return r;
}

const OpusFileCallbacks callbacks =
{
   OpusRead,
   OpusSeek,
   OpusTell,
   NULL,
};

} // end namespace

void audio::CreateOpusSource(
   common::Stream *file,
   CodecArgs &params,
   Source **obj,
   error *err
)
{
   Pointer<OpusFile> r;
   try
   {
      *r.GetAddressOf() = new OpusFile(file);
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
