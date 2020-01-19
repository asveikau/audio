/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <windows.h>
#include <mmsystem.h>

#include <common/misc.h>
#include <common/c++/new.h>
#include <AudioDevice.h>

#include <string.h>
#include <string>

#include "win.h"
 
#pragma comment(lib, "winmm.lib")

using namespace common;
using namespace audio;
using namespace audio::windows;

namespace {

void
error_set_winmm(error *err, MMRESULT res);

class WinMmDev : public Device
{
   HWAVEOUT waveOut;
   UINT_PTR deviceId;
   std::wstring descr;
   char *devName;
   HANDLE event;
   WAVEHDR buffers[3];
   int iBuffer;

   void CleanupOld(void)
   {
      if (waveOut)
      {
         if (iBuffer >= 0 && buffers[iBuffer].dwUser)
         {
            auto &p = buffers[iBuffer];

            p.dwBufferLength = p.dwUser;
            p.dwUser = 0;
            waveOutWrite(waveOut, &p, sizeof(p));
         }

         iBuffer = -1;

         waveOutReset(waveOut);

         for (int i=0; i<ARRAY_SIZE(buffers); ++i)
         {
            if (buffers[i].lpData)
            {
               waveOutUnprepareHeader(waveOut, buffers+i, sizeof(*buffers));
               free(buffers[i].lpData);
            }
         }

         waveOutClose(waveOut);
         waveOut = nullptr;

         memset(&buffers, 0, sizeof(buffers));
      }
   }

public:

   WinMmDev(UINT_PTR deviceId_, PCWSTR descr_)
      : waveOut(nullptr),
        deviceId(deviceId_),
        descr(descr_),
        devName(nullptr),
        event(nullptr),
        iBuffer(-1)
   {
      memset(&buffers, 0, sizeof(buffers));
   }

   ~WinMmDev()
   {
      CleanupOld();
      free(devName);
      if (event)
         CloseHandle(event);
   }

   void SetMetadata(const Metadata &metadata, error *err)
   {
      MMRESULT res = 0;
      WAVEFORMATEX fmt;
      int n = metadata.Channels * GetBitsPerSample(metadata.Format)/8
                                * metadata.SamplesPerFrame;

      CleanupOld();

      if (!event)
      {
         event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
         if (!event)
            ERROR_SET(err, win32, GetLastError());
      }

      MetadataToWaveFormatEx(metadata, &fmt);

      res = waveOutOpen(
         &waveOut,
         deviceId,
         &fmt,
         (DWORD_PTR)event,
         0,
         CALLBACK_EVENT | WAVE_ALLOWSYNC
      );
      if (res) ERROR_SET(err, winmm, res);

      for (int nBuffer = 0; nBuffer < ARRAY_SIZE(buffers); ++nBuffer)
      {
         auto &p = buffers[nBuffer];

         p.lpData = (PSTR)malloc(n);
         if (!p.lpData)
            ERROR_SET(err, nomem);
         memset(p.lpData, 0, n);
         p.dwBufferLength = n;
         res = waveOutPrepareHeader(waveOut, &p, sizeof(p));
         if (res) ERROR_SET(err, winmm, res);
         res = waveOutWrite(waveOut, &p, sizeof(p));
         if (res) ERROR_SET(err, winmm, res);
      }

   exit:;
   }

   void Write(const void *buf, int len, error *err)
   {
      int n;
      int res;
      WAVEHDR *p;

   retry:

      while (iBuffer < 0)
      {
         WaitForSingleObject(event, INFINITE);

         for (int i=0; i<ARRAY_SIZE(buffers); ++i)
         {
            p = &buffers[i];

            if ((p->dwFlags & WHDR_DONE))
            {
               p->dwFlags &= ~WHDR_DONE;
               p->dwUser = 0;
               iBuffer = i;
               break;
            }
         }
      }

      p = &buffers[iBuffer];
      n = MIN(len, p->dwBufferLength - p->dwUser);
      memcpy(p->lpData + p->dwUser, buf, n);
      p->dwUser += n;
      len -= n;
      buf = (const char*)buf + n;
      if (p->dwUser == p->dwBufferLength)
      {
         res = waveOutWrite(waveOut, p, sizeof(*p));
         if (res) ERROR_SET(err, winmm, res);
         iBuffer++;
         if (iBuffer == ARRAY_SIZE(buffers) || !(buffers[iBuffer].dwFlags & WHDR_DONE))
            iBuffer = -1;
      }
      if (len)
         goto retry;

   exit:;
   }

   const char *
   GetName(error *err)
   {
      if (devName) goto exit;

      try
      {
         devName = ConvertToPstr(descr.c_str(), err);
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
      ERROR_CHECK(err);
   
   exit:
      return devName;
   }

   bool ProbeSampleRate(int rate, error *err)
   {
      bool r = false;
      MMRESULT res = MMSYSERR_NOERROR;
      WAVEFORMATEX wfe;
      Metadata md;

      md.Format = PcmShort;
      md.SampleRate = rate;
      md.Channels = 2;

      MetadataToWaveFormatEx(md, &wfe);

      res = waveOutOpen(nullptr, deviceId, &wfe, 0, 0, WAVE_FORMAT_QUERY); 
      switch (res)
      {
      case MMSYSERR_NOERROR:
         r = true;
         break;
      case WAVERR_BADFORMAT:
         break;
      default:
         ERROR_SET(err, winmm, res);
      }

   exit:
      return r;
   }

   void GetSupportedSampleRates(SampleRateSupport &spec, error *err)
   {
      try
      {
         for (auto p = SampleRateSupport::GetCommonSampleRates(); *p; ++p)
         {
            auto probe = ProbeSampleRate(*p, err);
            ERROR_CHECK(err);
            if (probe)
               spec.rates.push_back(*p);
         }
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
   exit:;
   }

   void ProbeSampleRate(int rate, int &suggestion, error *err)
   {
      bool probe = ProbeSampleRate(rate, err);
      ERROR_CHECK(err);

      if (!probe)
         Device::ProbeSampleRate(rate, suggestion, err);
      ERROR_CHECK(err);
   exit:;
   }
};

struct WinMmEnumerator : public DeviceEnumerator
{
   void
   CreateDevice(UINT_PTR deviceId, Device **out, error *err)
   {
      MMRESULT res = 0;
      WAVEOUTCAPS caps = {0};
      Pointer<Device> dev;
      res = waveOutGetDevCaps(deviceId, &caps, sizeof(caps));
      if (res) ERROR_SET(err, winmm, res);
      try
      {
         *dev.GetAddressOf() = new WinMmDev(deviceId, caps.szPname);
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
   exit:
      *out = dev.Detach();
   }

public:

   int
   GetDeviceCount(error *err)
   {
      return waveOutGetNumDevs();
   }
   
   void
   GetDevice(int i, Device **out, error *err)
   {
      CreateDevice(i, out, err);
   }
   
   void
   GetDefaultDevice(Device **out, error *err)
   {
      CreateDevice(WAVE_MAPPER, out, err);
   }
}; 

void
error_set_winmm(error *err, MMRESULT res)
{
   const char *msg = "winmm error";

   switch (res)
   {
   case MMSYSERR_BADDEVICEID:
      msg = "The specified device ID is out of range.";
      break;
   case MMSYSERR_NOTENABLED:
      msg = "The driver failed to load or initialize.";
      break;
   case MMSYSERR_ALLOCATED:
      msg = "The specified device is already allocated.";
      break;
   case MMSYSERR_INVALHANDLE:
      msg = "The handle of the specified device is invalid.";
      break;
   case MMSYSERR_NODRIVER:
      msg = "No device driver is present.";
      break;
   case MMSYSERR_NOMEM:
      error_set_nomem(err);
      return;
   case MMSYSERR_NOTSUPPORTED:
      msg = "The function requested by the message is not supported.";
      break;
   case MMSYSERR_BADERRNUM:
      msg = "Error value is out of range.";
      break;
   case MMSYSERR_INVALFLAG:
      msg = "An invalid flag was passed to modMessage (by using dwParam2).";
      break;
   case MMSYSERR_INVALPARAM:
      msg = "An invalid parameter was passed to modMessage.";
      break;
   case MMSYSERR_HANDLEBUSY:
      msg = "The specified handle is being used simultaneously by another "
            "thread";
      break;
   case MMSYSERR_INVALIDALIAS:
      msg = "The specified alias was not found.";
      break;
   case MMSYSERR_BADDB:
      msg = "Bad registry database.";
      break;
   case MMSYSERR_KEYNOTFOUND:
      msg = "The specified registry key was not found.";
      break;
   case MMSYSERR_READERROR:
      msg = "Registry read error.";
      break;
   case MMSYSERR_WRITEERROR:
      msg = "Registry write error.";
      break;
   case MMSYSERR_DELETEERROR:
      msg = "Registry delete error.";
      break;
   case MMSYSERR_VALNOTFOUND:
      msg = "The specified registry value was not found.";
      break;
   case MMSYSERR_NODRIVERCB:
      msg = "The driver that works with modMessage does not call "
            "DriverCallback.";
      break;
   case MMSYSERR_MOREDATA:
      msg = "modMessage has more data to return.";
      break;
   case WAVERR_BADFORMAT:
      msg = "Bad format";
      break;
   case WAVERR_STILLPLAYING:
      msg = "Playback in progress";
      break;
   case WAVERR_UNPREPARED:
      msg = "Buffer not prepared";
      break;
   }

   error_set_unknown(err, msg);
}

} // namespace

void
audio::GetWinMmDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   Pointer<WinMmEnumerator> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *out = r.Detach();
}
