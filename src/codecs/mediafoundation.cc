/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <AudioCodec.h>
#include "seekbase.h"
#include <common/misc.h>
#include <common/c++/new.h>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <mutex>

#if !defined(_M_IX86)
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#endif
#pragma comment(lib, "mfuuid.lib")

using namespace Microsoft::WRL;

using namespace common;
using namespace audio;

namespace {

HRESULT
CreateStreamWrapper(
   Stream *stream,
   const audio::CodecArgs &args,
   IMFByteStream **mfStream
);

HRESULT
MFStartup_AddRef(VOID);

VOID
MFStartup_Release(VOID);

class MFSource : public Source
{
   Pointer<Stream> stream;
   ComPtr<IMFSourceReader> reader;
   ComPtr<IMFSample> currentSample;
   INT currentBufferIndex;
   ComPtr<IMFMediaBuffer> currentBuffer;
   PBYTE currentLockedBuffer;
   DWORD currentLockedBufferLen;
   bool eof;
   ULONGLONG cachedDuration;
   char description[128];

   VOID UnlockBuffer()
   {
      if (currentLockedBuffer)
      {
         currentBuffer->Unlock();
         currentLockedBuffer = nullptr;
         currentLockedBufferLen = 0;
      }
   }

public:

   MFSource() :
      currentBufferIndex(0),
      currentLockedBuffer(nullptr),
      currentLockedBufferLen(0),
      eof(false),
      cachedDuration(0)
   {
   }

   ~MFSource()
   {
      UnlockBuffer();
      currentBuffer = nullptr;
      currentSample = nullptr;
      reader = nullptr;
      MFStartup_Release();
   }

   const char *Describe(void)
   {
      static const char tag[] = "[mediafoundation]";
      HRESULT hr = S_OK;
      ComPtr<IMFMediaType> mediaType;
      GUID codecGuid = {0};
      char guidBuffer[46];
      const char *codec = nullptr;

      hr = reader->GetNativeMediaType(
         MF_SOURCE_READER_FIRST_AUDIO_STREAM,
         0,
         mediaType.GetAddressOf()
      );
      if (FAILED(hr)) goto exit;
      hr = mediaType->GetGUID(MF_MT_SUBTYPE, &codecGuid);
      if (FAILED(hr)) goto exit;

      codec = LookupCodecGuid(codecGuid);
      if (!codec)
      {
         snprintf(
            guidBuffer,
            sizeof(guidBuffer),
            "codec {%.8x-%.4x-%.4x-%.2x%.2x-%.2x%.2x%.2x%.2x%.2x%.2x}",
            codecGuid.Data1,
            codecGuid.Data2,
            codecGuid.Data3,
            codecGuid.Data4[0],
            codecGuid.Data4[1],
            codecGuid.Data4[2],
            codecGuid.Data4[3],
            codecGuid.Data4[4],
            codecGuid.Data4[5],
            codecGuid.Data4[6],
            codecGuid.Data4[7]
         );
         codec = guidBuffer;
      }

      snprintf(description, sizeof(description), "%s %s", tag, codec);
   exit:
      return SUCCEEDED(hr) ? description : tag;
   }

   void Initialize(Stream *file, CodecArgs &params, error *err)
   {
      HRESULT hr = S_OK;
      ComPtr<IMFByteStream> stream;
      ComPtr<IMFAttributes> attrs;
      ComPtr<IMFMediaType> pcmMediaType;

      this->stream = file;

      cachedDuration = params.Duration;

      ContainerHasSlowSeek = IsSlowSeekContainer(file, err);
      ERROR_CHECK(err);

      hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = MFStartup_AddRef();
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = CreateStreamWrapper(file, params, stream.GetAddressOf());
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = MFCreateAttributes(attrs.GetAddressOf(), 0);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = MFCreateSourceReaderFromByteStream(
         stream.Get(),
         attrs.Get(),
         reader.GetAddressOf()
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = reader->SetStreamSelection(
         MF_SOURCE_READER_FIRST_AUDIO_STREAM,
         TRUE
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = MFCreateMediaType(pcmMediaType.GetAddressOf());
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = pcmMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = pcmMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = reader->SetCurrentMediaType(
         MF_SOURCE_READER_FIRST_AUDIO_STREAM,
         nullptr,
         pcmMediaType.Get()
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = reader->SetStreamSelection(
         MF_SOURCE_READER_FIRST_AUDIO_STREAM,
         TRUE
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

   exit:;
   }

   VOID
   ReadSample(error *err)
   {
      HRESULT hr = S_OK;
      DWORD flags = 0;
      LONGLONG ts = 0;

      UnlockBuffer();
      currentBuffer = nullptr;
      currentBufferIndex = 0;

   retry:
      hr = reader->ReadSample(
         MF_SOURCE_READER_FIRST_AUDIO_STREAM,
         0,
         nullptr,
         &flags,
         &ts,
         currentSample.ReleaseAndGetAddressOf()
      ); 
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      if ((flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)))
         eof = true;              
      if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))
         MetadataChanged = true;
      if (!eof && !currentSample.Get())
         goto retry;
   exit:;
   }

   void GetMetadata(Metadata *res, error *err)
   {
      HRESULT hr = S_OK;
      ComPtr<IMFMediaType> mediaType;
      PWAVEFORMATEX waveFormat = nullptr;
      UINT32 waveFormatLength = 0;

      // Workaround for old bug, possibly nonexistent these days.
      // Querying audio format before any decode has happened would sometimes
      // yield incorrect info.  (eg. mono file reported as stereo)  Force a
      // decode to happen.
      //
      if (!eof && !currentSample.Get())
      {
         ReadSample(err);
         ERROR_CHECK(err);

         MetadataChanged = false;
      }

      hr = reader->GetCurrentMediaType(
         MF_SOURCE_READER_FIRST_AUDIO_STREAM,
         mediaType.GetAddressOf()
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = MFCreateWaveFormatExFromMFMediaType(
         mediaType.Get(),
         &waveFormat,
         &waveFormatLength
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      switch (waveFormat->wFormatTag)
      {
      case WAVE_FORMAT_PCM:
         res->Format = PcmShort;
         break;
      default:
         ERROR_SET(err, unknown, "Unknown format tag");
      }

      res->Channels = waveFormat->nChannels;
      res->SampleRate = waveFormat->nSamplesPerSec;

      if (currentSample.Get())
      {
         DWORD len = 0;
         hr = currentSample->GetTotalLength(&len);
         if (FAILED(hr)) ERROR_SET(err, win32, hr);
         res->SamplesPerFrame =
            len / (res->Channels * GetBitsPerSample(res->Format)/8);
      }
      else
      {
         res->SamplesPerFrame = 0;
      }

   exit:
      if (waveFormat)
         CoTaskMemFree(waveFormat);
   }

   int Read(void *buf, int len, error *err)
   {
      int r = 0;
      HRESULT hr = S_OK;

      if (!len || MetadataChanged)
         goto exit;

      if (!currentSample.Get())
      {
         if (eof)
            goto exit;

         ReadSample(err);
         ERROR_CHECK(err);

         if (MetadataChanged)
            goto exit;
         if (eof && !currentSample.Get())
            goto exit;
      }

      if (!currentBuffer.Get() || !currentLockedBufferLen)
      {
      next_buffer:
         DWORD nBuffers = 0;
         UnlockBuffer();
         hr = currentSample->GetBufferCount(&nBuffers);
         if (FAILED(hr)) ERROR_SET(err, win32, hr); 
         if (currentBufferIndex >= nBuffers)
         {
            currentSample = nullptr;
            currentBuffer = nullptr;
            currentBufferIndex = 0;
            goto exit;
         }
         hr = currentSample->GetBufferByIndex(
            currentBufferIndex,
            currentBuffer.ReleaseAndGetAddressOf()
         );
         if (FAILED(hr)) ERROR_SET(err, win32, hr); 
         ++currentBufferIndex;
      }

      if (!currentLockedBuffer)
      {
         hr = currentBuffer->Lock(
            &currentLockedBuffer,
            nullptr,
            &currentLockedBufferLen
         );
         if (FAILED(hr)) ERROR_SET(err, win32, hr); 
      }

      int n = MIN(len, currentLockedBufferLen);
      if (n)
      {
         memcpy(buf, currentLockedBuffer, n);
         buf = ((char*)buf) + n;
         len -= n;
         currentLockedBuffer += n;
         currentLockedBufferLen -= n;
         r += n;

         if (!currentLockedBufferLen)
            goto next_buffer;
      }

   exit:
      return r;
   }

   void Seek(uint64_t pos, error *err)
   {
      HRESULT hr = S_OK;
      PROPVARIANT prop;
      PropVariantInit(&prop);

      hr = reader->Flush(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      UnlockBuffer();
      eof = false;

      currentSample = nullptr;
      currentBuffer = nullptr;
      currentBufferIndex = 0;

      prop.vt = VT_I8;
      prop.uhVal.QuadPart = pos;

      hr = reader->SetCurrentPosition(GUID_NULL, prop);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

   exit:
      PropVariantClear(&prop);
   }

   uint64_t GetPosition(error *err)
   {
      HRESULT hr = S_OK;
      LONGLONG ts = 0LL;
      LONGLONG offset = 0LL;

      if (eof)
         return GetDuration(err);

      if (!currentSample.Get())
      {
         ReadSample(err);
         ERROR_CHECK(err);

         if (!currentSample.Get())
            return GetDuration(err);
      }

      hr = currentSample->GetSampleTime(&ts);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      for (int i=0; i<currentBufferIndex; ++i)
      {
         ComPtr<IMFMediaBuffer> buf;
         DWORD size = 0;
         hr = currentSample->GetBufferByIndex(i, buf.GetAddressOf());
         if (FAILED(hr)) ERROR_SET(err, win32, hr);
         hr = buf->GetCurrentLength(&size);
         if (FAILED(hr)) ERROR_SET(err, win32, hr);
         offset += size;
      }

      if (currentBuffer.Get())
      {
         DWORD size = 0;
         hr = currentBuffer->GetCurrentLength(&size);
         if (FAILED(hr)) ERROR_SET(err, win32, hr);
         offset += (size - currentLockedBufferLen);
      }

      if (offset)
      {
         Metadata md;
         GetMetadata(&md, err);
         ERROR_CHECK(err);

         offset /= (md.Channels * GetBitsPerSample(md.Format)/8);
         ts += (offset * 10000000LL / md.SampleRate);
      }

   exit:
      return ts;
   }

   uint64_t GetDuration(error *err)
   {
      HRESULT hr = S_OK;
      PROPVARIANT prop;
      uint64_t r = 0;

      PropVariantInit(&prop);

      if (cachedDuration)
      {
         r = cachedDuration;
         goto exit;
      }

      hr = reader->GetPresentationAttribute(
         MF_SOURCE_READER_MEDIASOURCE,
         MF_PD_DURATION,
         &prop
      ); 
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      r = prop.uhVal.QuadPart;
      cachedDuration = r;

   exit:
      PropVariantClear(&prop);
      return r;
   }

   void GetStreamInfo(audio::StreamInfo *info, error *err)
   {
      info->DurationKnown = cachedDuration != 0;

      stream->GetStreamInfo(&info->FileStreamInfo, err);
      ERROR_CHECK(err);

      Source::GetStreamInfo(info, err);
      ERROR_CHECK(err);
   exit:;
   }

private:

   const char *LookupCodecGuid(const GUID &guid)
   {
      static const GUID
      MFAudioFormat_ALAC_Observed =
      {
         0x616c6163U,
         0x767a,
         0x494d,
         {0xb4,0x78,0xf2,0x9d,0x25,0xdc,0x90,0x37}
      };

      static const struct
      {
         GUID Guid;
         const char *Description;
      } Mappings[] =
      {
#if 0
         {MEDIASUBTYPE_RAW_AAC1,         "AAC, AVI container"},
#endif
         {MFAudioFormat_AAC,                 "AAC"},
         {MFAudioFormat_ADTS,                "AAC (ADTS)"},
         {MFAudioFormat_ALAC,                "Apple Lossless"},
         {MFAudioFormat_ALAC_Observed,       "Apple Lossless"},
         {MFAudioFormat_AMR_NB,              "AMR"},
         {MFAudioFormat_AMR_WB,              "AMR-WB"},
         {MFAudioFormat_Dolby_AC3,           "AC3"},
         {MFAudioFormat_Dolby_AC3_SPDIF,     "AC3 (S/PDIF)"},	
         {MFAudioFormat_Dolby_DDPlus,        "Dolby Digital Plus"},
         {MFAudioFormat_DTS,                 "DTS"},
         {MFAudioFormat_FLAC,                "FLAC"},
         {MFAudioFormat_Float,               "PCM Float"},
#if 0
         {MFAudioFormat_Float_SpatialObjects,"PCM Float (SpatialObjects)"},
#endif
         {MFAudioFormat_MP3,                 "MPEG Layer 3"},
         {MFAudioFormat_MPEG,                "MPEG"},
         {MFAudioFormat_MSP1,                "WMA9 Voice"},
#if 0
         {MFAudioFormat_Opus,                "Opus"},
#endif
         {MFAudioFormat_PCM,                 "PCM"},
#if 0
         {MFAudioFormat_QCELP,               "QCELP"},
#endif
         {MFAudioFormat_WMASPDIF,            "WMA9 (S/PDIF)"},
         {MFAudioFormat_WMAudio_Lossless,    "WMA9 Lossless"},
         {MFAudioFormat_WMAudioV8,           "WMA8"},
         {MFAudioFormat_WMAudioV9,           "WMA9"},
         {GUID_NULL, NULL}
      };
      auto p = Mappings;
      while (p->Description)
         if (IsEqualGUID(guid, p->Guid))
            return p->Description;
         else
            ++p;
      return nullptr;
   }
};

struct MFFactory : public Codec
{
   void TryOpen(
      Stream *file,
      const void *firstBuffer,
      int firstBufferSize,
      CodecArgs &params,
      Source **obj,
      error *err 
   )
   {
      Pointer<MFSource> src;
      New(src.GetAddressOf(), err);
      ERROR_CHECK(err);
      src->Initialize(file, params, err);
      ERROR_CHECK(err);

      *obj = src.Detach();
      return;
   exit:;
   }
};

std::mutex initLock;
int initCount;

HRESULT
MFStartup_AddRef(VOID)
{
   std::lock_guard<std::mutex> lock(initLock);
   if (initCount == 0)
   {
      HRESULT hr = MFStartup(MF_VERSION);
      if (FAILED(hr))
         return hr;
   }
   ++initCount;
   return S_OK;
}

VOID
MFStartup_Release(VOID)
{
   std::lock_guard<std::mutex> lock(initLock);
   if (--initCount)
   {
      MFShutdown();
   }
}

struct IoWrapper
   : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUnknown>
{
   DWORD bytesTransferred;
};

class StreamWrapper
   : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFByteStream>
{
   Pointer<Stream> stream;
   bool slowSeek;

   HRESULT HrFromErr(error *err)
   {
      HRESULT hr = S_OK;

      switch (err->source)
      {
      case ERROR_SRC_SUCCESS:
         break;
      case ERROR_SRC_COM:
         hr = err->code;
         break;
      default:
         error_log(err, ERROR_FUNCTION_MACRO, __FILE__, __LINE__);
         hr = E_FAIL;
      }

      return hr;
   }

public:

   StreamWrapper() : slowSeek(false) {}

   HRESULT CALLBACK
   RuntimeClassInitialize(
      Stream *stream,
      const audio::CodecArgs &args
   )
   {
      this->stream = stream;

      error err;
      common::StreamInfo info;
      stream->GetStreamInfo(&info, &err);
      if (ERROR_FAILED(&err))
         return E_FAIL;

      this->slowSeek = info.IsRemote;

      return S_OK;
   }

   HRESULT CALLBACK
   Close(VOID)
   {
      return S_OK;
   }

   HRESULT CALLBACK
   Read(PBYTE buf, ULONG len, ULONG *bytesIn)
   {
      error err;
      *bytesIn = stream->Read(buf, len, &err);
      ERROR_CHECK(&err);
   exit:
      return HrFromErr(&err);
   }

   HRESULT CALLBACK
   Write(const BYTE *buf, ULONG len, ULONG *bytesOut)
   {
      error err;
      *bytesOut = stream->Write(buf, len, &err);
      ERROR_CHECK(&err);
   exit:
      return HrFromErr(&err);
   }

   HRESULT CALLBACK
   GetCapabilities(DWORD *caps)
   {
      *caps = MFBYTESTREAM_IS_READABLE |
              MFBYTESTREAM_IS_SEEKABLE;

      if (slowSeek)
         *caps |= (MFBYTESTREAM_IS_REMOTE | MFBYTESTREAM_HAS_SLOW_SEEK);

      return S_OK;
   }

   HRESULT CALLBACK
   BeginRead(PBYTE buf, ULONG len, IMFAsyncCallback *cb, IUnknown *pUnk)
   {
      HRESULT hr = S_OK;
      ComPtr<IoWrapper> wrapper;
      ComPtr<IMFAsyncResult> res;

      hr = MakeAndInitialize<IoWrapper>(wrapper.GetAddressOf());
      if (FAILED(hr))
         return hr;

      hr = MFCreateAsyncResult(
         wrapper.Get(),
         cb,
         pUnk,
         res.GetAddressOf()
      ); 
      if (FAILED(hr))
         return hr;
      hr = Read(buf, len, &wrapper->bytesTransferred);
      if (FAILED(hr))
         return hr;
      res->SetStatus(hr);
      hr = MFInvokeCallback(res.Get());
      return hr;
   }

   HRESULT CALLBACK
   EndRead(IMFAsyncResult *res, ULONG *bytesIn)
   {
      ComPtr<IoWrapper> wrapper;
      res->GetObject((IUnknown**)wrapper.GetAddressOf());
      *bytesIn = wrapper->bytesTransferred;
      return res->GetStatus();
   }

   HRESULT CALLBACK
   BeginWrite(const BYTE *buf, ULONG len, IMFAsyncCallback *cb, IUnknown *pUnk)
   {
      HRESULT hr = S_OK;
      ComPtr<IoWrapper> wrapper;
      ComPtr<IMFAsyncResult> res;

      hr = MakeAndInitialize<IoWrapper>(wrapper.GetAddressOf());
      if (FAILED(hr))
         return hr;

      hr = MFCreateAsyncResult(
         wrapper.Get(),
         cb,
         pUnk,
         res.GetAddressOf()
      ); 
      if (FAILED(hr))
         return hr;
      hr = Write(buf, len, &wrapper->bytesTransferred);
      if (FAILED(hr))
         return hr;
      res->SetStatus(hr);
      hr = MFInvokeCallback(res.Get());
      return hr;
   }

   HRESULT CALLBACK
   EndWrite(IMFAsyncResult *res, ULONG *bytesOut)
   {
      ComPtr<IoWrapper> wrapper;
      res->GetObject((IUnknown**)wrapper.GetAddressOf());
      *bytesOut = wrapper->bytesTransferred;
      return res->GetStatus();
   }

   HRESULT CALLBACK
   Flush(VOID)
   {
      error err;
      stream->Flush(&err);
      ERROR_CHECK(&err);
   exit:
      return HrFromErr(&err);
   }

   HRESULT CALLBACK
   GetCurrentPosition(QWORD *res)
   {
      error err;
      *res = stream->GetPosition(&err);
      ERROR_CHECK(&err);
   exit:
      return HrFromErr(&err);
   }

   HRESULT CALLBACK
   GetLength(QWORD *res)
   {
      error err;
      *res = stream->GetSize(&err);
      ERROR_CHECK(&err);
   exit:
      return HrFromErr(&err);
   }

   HRESULT CALLBACK
   IsEndOfStream(BOOL *res)
   {
      HRESULT hr = S_OK;
      QWORD size = 0, pos = 0;
      hr = GetLength(&size);
      if (SUCCEEDED(hr))
         hr = GetCurrentPosition(&pos);
      *res = SUCCEEDED(hr) && pos >= size;
      return hr; 
   }

   HRESULT CALLBACK
   Seek(MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG off, DWORD flags, QWORD *np)
   {
      error err;
      int o = 0;
      switch (origin)
      {
      case msoBegin:
         o = SEEK_SET;
         break;
      case msoCurrent:
         o = SEEK_CUR;
         break;
      default:
         ERROR_SET(&err, unknown, "Unknown seek origin");
      }
      stream->Seek(off, o, &err);
      ERROR_CHECK(&err);
      *np = stream->GetPosition(&err);
      ERROR_CHECK(&err);
   exit:
      return HrFromErr(&err);
   }

   HRESULT CALLBACK
   SetCurrentPosition(QWORD off)
   {
      error err;
      stream->Seek(off, SEEK_SET, &err);
      ERROR_CHECK(&err);
   exit:
      return HrFromErr(&err);
   }

   HRESULT CALLBACK
   SetLength(QWORD len)
   {
      error err;
      stream->Truncate(len, &err);
      ERROR_CHECK(&err);
   exit:
      return HrFromErr(&err);
   }
};

HRESULT
CreateStreamWrapper(Stream *stream, const audio::CodecArgs &args, IMFByteStream **mfStream)
{
   return MakeAndInitialize<StreamWrapper>(mfStream, stream, args);
}

}

void audio::RegisterMfCodec(error *err)
{
   Pointer<MFFactory> p;
   New(p.GetAddressOf(), err);
   ERROR_CHECK(err);
   RegisterCodec(p.Get(), err);
   ERROR_CHECK(err);
exit:;
}

