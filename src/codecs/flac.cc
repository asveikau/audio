/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include <common/misc.h>
#include <common/logger.h>
#include <common/c++/new.h>

using namespace common;
using namespace audio;

#include <string.h>
#include <errno.h>
#include <vector>

#include <FLAC/stream_decoder.h>

namespace {

class FlacSource : public Source
{
   Pointer<Stream> stream;
   FLAC__StreamDecoder *file;
   bool eof;
   void *currentBuffer;
   int currentLen;
   std::vector<unsigned char> pendingSamples;
   int channels;
   int sampleRate;
   int bitsPerSample;
   uint64_t currentPos;
   bool isOgg;
   char description[128];
   MetadataReceiver recv;
public:

   FlacSource(Stream *stream_, bool ogg) :
      stream(stream_),
      file(nullptr),
      eof(false),
      currentBuffer(nullptr),
      currentLen(0),
      currentPos(0),
      isOgg(ogg)
   {
      file = FLAC__stream_decoder_new();
      if (!file)
         throw std::bad_alloc();
   }

   ~FlacSource()
   {
      FLAC__stream_decoder_delete(file);
   }

   const char *Describe(void)
   {
      snprintf(
        description,
        sizeof(description),
        "[flac] %ssrc bps=%d",
        isOgg ? "ogg container, " : "",
        FLAC__stream_decoder_get_bits_per_sample(file)
      );
      return description; 
   }

   void
   Initialize(MetadataReceiver *recv, error *err)
   {
      FLAC__StreamDecoderInitStatus status;

      if (recv)
      {
         try
         {
            this->recv = *recv;
            recv = &this->recv;
         }
         catch (const std::bad_alloc&)
         {
            ERROR_SET(err, nomem);
         }
      }

      status =
        (isOgg ? FLAC__stream_decoder_init_ogg_stream
               : FLAC__stream_decoder_init_stream)(
           file,
           ReadCallback,
           SeekCallback,
           TellCallback,
           LengthCallback,
           EofCallback,
           WriteCallback,
           recv ? MetadataCallback : nullptr,
           ErrorCallback,
           this
        );
      if (status)
         ERROR_SET(err, unknown, FLAC__StreamDecoderInitStatusString[status]);

      while (!FLAC__stream_decoder_get_sample_rate(file))
      {
         if (!FLAC__stream_decoder_process_single(file))
            ERROR_SET(err, unknown, "decoder error");
      }

      MetadataChanged = false;
      channels = FLAC__stream_decoder_get_channels(file);
      sampleRate = FLAC__stream_decoder_get_sample_rate(file);
      bitsPerSample = FLAC__stream_decoder_get_bits_per_sample(file);
   exit:;
   }

   void
   GetMetadata(Metadata *res, error *err)
   {
      res->Format = PcmShort;
      res->SampleRate = FLAC__stream_decoder_get_sample_rate(file);
      res->Channels = FLAC__stream_decoder_get_channels(file);
      res->SamplesPerFrame = 0;
   }

   int
   Read(void *buf, int len, error *err)
   {
      int r = 0;

      if (!len)
         goto exit;

      if (pendingSamples.size())
      {
         int n = MIN(pendingSamples.size(), len);
         memcpy(buf, pendingSamples.data(), n);
         buf = (char*)buf + n;
         len -= n;
         r += n;
         pendingSamples.erase(pendingSamples.begin(), pendingSamples.begin() + n);
         if (!len)
            goto exit;
      }

      currentBuffer = buf;
      currentLen = len;

      if (!FLAC__stream_decoder_process_single(file))
         ERROR_SET(err, unknown, "decoder error");

      r += (len - currentLen);

   exit:
      currentBuffer = nullptr;
      currentLen = 0;
      currentPos += r / (channels * bitsPerSample / 8);
      return r;
   }

   void
   OnSample(int16_t sample)
   {
      void *res;

      if (!MetadataChanged && currentLen >= 2)
      {
         res = currentBuffer;
         currentBuffer = (char*)currentBuffer + 2;
         currentLen -= 2;
      }
      else
      {
         pendingSamples.push_back(0);
         pendingSamples.push_back(0);
         res = pendingSamples.data() + pendingSamples.size() - 2;
      }

      memcpy(res, &sample, sizeof(sample));
   }

   void
   OnFrameDecoded(
      const FLAC__Frame *frame,
      const FLAC__int32 * const buffer[],
      error *err
   )
   {
      const auto channels = frame->header.channels;
      const auto bps = frame->header.bits_per_sample;
      float conversion = 1.0f;

      if (channels != this->channels ||
          bps != bitsPerSample ||
          sampleRate != frame->header.sample_rate)
      {
         MetadataChanged = true;

         this->channels = channels;
         this->bitsPerSample = bps;
         this->sampleRate = frame->header.sample_rate;
      }

      if (bps != 16)
         conversion = 1.0f / (1LL << (bps-1)) * 32767.0;

      try
      {
         for (int i = 0; i < frame->header.blocksize; i++)
         {
            for (int channel = 0; channel < channels; ++channel)
            {
               auto sample = buffer[channel][i];
               if (bps == 16)
                  OnSample(sample);
               else
                  OnSample(sample * conversion);
            }
         }
      }
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   void Seek(uint64_t pos, error *err)
   {
      auto samplePos = pos * FLAC__stream_decoder_get_sample_rate(file)
                          / 10000000LL;
      if (!FLAC__stream_decoder_seek_absolute(file, samplePos))
      {
         ERROR_SET(err, unknown, "Failed to seek");
      }
      pendingSamples.resize(0);
      currentPos = samplePos;
   exit:;
   }

   uint64_t
   GetPosition(error *err)
   {
      return currentPos * 10000000LL / 
             FLAC__stream_decoder_get_sample_rate(file);
   }

   uint64_t
   GetDuration(error *err)
   {
      return FLAC__stream_decoder_get_total_samples(file) * 10000000LL / 
             FLAC__stream_decoder_get_sample_rate(file);
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

   static FLAC__StreamDecoderWriteStatus
   WriteCallback(
      const FLAC__StreamDecoder *decoder,
      const FLAC__Frame *frame,
      const FLAC__int32 * const buffer[],
      void *client_data
   )
   {
      auto This = (FlacSource*)client_data;
      FLAC__StreamDecoderWriteStatus status = FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
      error err;

      This->OnFrameDecoded(frame, buffer, &err);
      ERROR_CHECK(&err);

   exit:
      if (ERROR_FAILED(&err)) status = FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
      return status;
   }

   static void
   ErrorCallback(
      const FLAC__StreamDecoder *decoder,
      FLAC__StreamDecoderErrorStatus status,
      void *client_data
   )
   {
      log_printf("flac: %s", FLAC__StreamDecoderErrorStatusString[status]);
   }

   static FLAC__StreamDecoderReadStatus
   ReadCallback(
      const FLAC__StreamDecoder *decoder,
      FLAC__byte buffer[],
      size_t *bytes,
      void *client_data 
   )
   {
      auto This = (FlacSource*)client_data;
      FLAC__StreamDecoderReadStatus status = FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
      error err;

      *bytes = This->stream->Read(buffer, *bytes, &err);
      ERROR_CHECK(&err);

      if (!*bytes)
      {
         status = FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
         This->eof = true;
      }

   exit:
      if (ERROR_FAILED(&err)) status = FLAC__STREAM_DECODER_READ_STATUS_ABORT;
      return status;
   }

   static FLAC__StreamDecoderSeekStatus
   SeekCallback(
      const FLAC__StreamDecoder *decoder, FLAC__uint64 offset, void *client_data
   )
   {
      auto This = (FlacSource*)client_data;
      FLAC__StreamDecoderSeekStatus status = FLAC__STREAM_DECODER_SEEK_STATUS_OK;
      error err;

      This->stream->Seek(offset, SEEK_SET, &err);
      ERROR_CHECK(&err);

      This->eof = false;

   exit:
      if (ERROR_FAILED(&err)) status = FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
      return status;
   }

   static FLAC__StreamDecoderTellStatus
   TellCallback(
      const FLAC__StreamDecoder *decoder, FLAC__uint64 *offset, void *client_data
   )
   {
      auto This = (FlacSource*)client_data;
      error err;
      FLAC__StreamDecoderTellStatus status = FLAC__STREAM_DECODER_TELL_STATUS_OK;

      *offset = This->stream->GetPosition(&err);
      ERROR_CHECK(&err);

   exit:
      if (ERROR_FAILED(&err)) status = FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
      return status;
   }

   static FLAC__StreamDecoderLengthStatus
   LengthCallback(
      const FLAC__StreamDecoder *decoder, FLAC__uint64 *length, void *client_data
   )
   {
      auto This = (FlacSource*)client_data;
      error err;
      FLAC__StreamDecoderLengthStatus status = FLAC__STREAM_DECODER_LENGTH_STATUS_OK;

      *length = This->stream->GetSize(&err);
      ERROR_CHECK(&err);

   exit:
      if (ERROR_FAILED(&err)) status = FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
      return status;
   }

   static FLAC__bool
   EofCallback(
      const FLAC__StreamDecoder *decoder, void *client_data
   )
   {
      auto This = (FlacSource*)client_data;
      return This->eof;
   }

   static void
   MetadataCallback(
      const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data
   )
   {
      auto recv = &((FlacSource*)client_data)->recv;
      if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT)
      {
         error err;

         auto vc = metadata->data.vorbis_comment;
         std::vector<char *> strings;
         std::vector<int> lengths;

         try
         {
            for (int i=0; i<vc.num_comments; ++i)
            {
               auto &comment = vc.comments[i];
               strings.push_back((char*)comment.entry);
               lengths.push_back(comment.length);
            }
         }
         catch (const std::bad_alloc&)
         {
            ERROR_SET(&err, nomem);
         }

         OnOggComments(
            recv,
            strings.data(),
            lengths.data(),
            vc.num_comments,
            (char*)vc.vendor_string.entry,
            &err
         );
         ERROR_CHECK(&err);
      }
   exit:;
   }
};

struct FlacCodec : public Codec
{
   int GetBytesRequiredForDetection() { return 4; }

   void TryOpen(
      common::Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err
   )
   {
      static const char magic[] = "fLaC"; 
      if (!memcmp(firstBuffer, magic, sizeof(magic)-1))
         CreateFlacSource(file, false, params, obj, err);
   }
};

} // end namespace

void audio::CreateFlacSource(
   common::Stream *file,
   bool isOgg,
   CodecArgs &params,
   Source **obj,
   error *err
)
{
   Pointer<FlacSource> r;
   try
   {
      *r.GetAddressOf() = new FlacSource(file, isOgg);
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

void audio::RegisterFlacCodec(error *err)
{
   Pointer<FlacCodec> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
   RegisterCodec(p.Get(), err);
   ERROR_CHECK(err);
exit:;
}

#ifdef _WIN32
#include <share/compat.h>

FILE *
flac_fopen(const char *filename, const char *mode)
{
   return nullptr;
}
#endif

