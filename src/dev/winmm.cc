/*
 Copyright (C) 2017-2018, 2020-2022 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

#include <common/misc.h>
#include <common/c++/new.h>
#include <common/uname.h>
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

   HWAVEOUT
   GetHandle()
   {
      return waveOut;
   }

   void
   GetSupportedFormats(const Format *&formats, int &n, error *err)
   {
      // Don't allow "fancy" formats for WinXP.
      //
      int version = 10;
      struct utsname os_version;
      uname(&os_version);
      sscanf(os_version.release, "%d", &version);
      if (version < 6)
      {
         Device::GetSupportedFormats(formats, n, err);
         return;
      }

      static const Format workingFormats[] =
      {
         PcmShort,
         Pcm24,
         Pcm24Pad,
      };
      formats = workingFormats;
      n = ARRAY_SIZE(workingFormats);
   }

   void SetMetadata(const Metadata &metadata, error *err)
   {
      MMRESULT res = 0;
      WAVEFORMATEXTENSIBLE fmt;
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
         &fmt.Format,
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
      catch (const std::bad_alloc&)
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
      WAVEFORMATEXTENSIBLE wfe;
      Metadata md;

      md.Format = PcmShort;
      md.SampleRate = rate;
      md.Channels = 2;

      MetadataToWaveFormatEx(md, &wfe);

      res = waveOutOpen(nullptr, deviceId, &wfe.Format, 0, 0, WAVE_FORMAT_QUERY);
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
      catch (const std::bad_alloc&)
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

class WinMmMixer : public Mixer
{
   HMIXER mixer;
   char *onHeap;

   struct ControlInfo
   {
      DWORD type;
      DWORD destination;
      DWORD control;
      DWORD channels;
      DWORD lineid;
      DWORD multiple;
   };

   std::vector<ControlInfo> controls;
   std::map<int, ControlInfo> muteControls;

public:
   WinMmMixer() : mixer(nullptr), onHeap(nullptr)
   {
   }

   ~WinMmMixer()
   {
      if (mixer)
         mixerClose(mixer);
      free(onHeap);
   }

   void
   Initialize(UINT id, DWORD flags, error *err)
   {
      MIXERCAPS caps = {0};
      MIXERCONTROL *controls = nullptr;
      std::vector<ControlInfo> muteControls;

      MMRESULT r = mixerOpen(&mixer, id, 0, 0, flags);
      if (r)
         ERROR_SET(err, winmm, r);

      r = mixerGetID((HMIXEROBJ)mixer, &id, MIXER_OBJECTF_HMIXER);
      if (r)
         ERROR_SET(err, winmm, r);

      r = mixerGetDevCaps(id, &caps, sizeof(caps));
      if (r)
         ERROR_SET(err, winmm, r);

      for (DWORD i = 0; i<caps.cDestinations; ++i)
      {
         MIXERLINE line = {0};

         line.cbStruct = sizeof(line);
         line.dwDestination = i;

         r = mixerGetLineInfo(
            (HMIXEROBJ)mixer,
            &line,
            MIXER_GETLINEINFOF_DESTINATION | MIXER_OBJECTF_HMIXER
         );
         if (r)
            ERROR_SET(err, winmm, r);

         if (!line.cControls)
            continue;

         if (controls)
            delete [] controls;
         controls = new (std::nothrow) MIXERCONTROL[line.cControls];
         if (!controls)
            ERROR_SET(err, nomem);

         MIXERLINECONTROLS lineControls = {0};

         lineControls.cbStruct = sizeof(lineControls);
         lineControls.dwLineID = line.dwLineID;
         lineControls.cControls = line.cControls;
         lineControls.pamxctrl = controls;
         lineControls.cbmxctrl = sizeof(*controls);

         r = mixerGetLineControls((HMIXEROBJ)mixer, &lineControls, MIXER_GETLINECONTROLSF_ALL | MIXER_OBJECTF_HMIXER);
         if (r)
            ERROR_SET(err, winmm, r);

         for (DWORD j=0; j<lineControls.cControls; ++j)
         {
            ControlInfo info;
            auto controlsP = &this->controls;

            switch (controls[j].dwControlType)
            {
            case MIXERCONTROL_CONTROLTYPE_VOLUME:
               break;
            case MIXERCONTROL_CONTROLTYPE_MUTE:
               controlsP = &muteControls;
               break;
            default:
               continue;
            }

            info.type = controls[j].dwControlType;
            info.destination = i;
            info.control = controls[j].dwControlID;
            info.channels = line.cChannels;
            info.lineid = line.dwLineID;
            info.multiple = controls[j].cMultipleItems;

            try
            {
               controlsP->push_back(info);
            }
            catch (const std::bad_alloc&)
            {
               ERROR_SET(err, nomem);
            }
         }
      }

      // Match mute switches to the appropriate index.
      //
      if (muteControls.size())
      {
         for (auto mc : muteControls)
         {
            // XXX O(n^2)
            for (int i=0; i<this->controls.size(); ++i)
            {
               const auto &control = this->controls[i];
               if (control.destination == mc.destination &&
                   control.lineid == mc.lineid)
               {
                  try
                  {
                     this->muteControls[i] = mc;
                  }
                  catch (const std::bad_alloc &)
                  {
                     ERROR_SET(err, nomem);
                  }
               }
            }
         }
      }
   exit:
      if (controls)
         delete [] controls;
   }

   int
   GetValueCount(error *err)
   {
      return controls.size();
   }

   const char *
   DescribeValue(int idx, error *err)
   {
      MIXERCONTROLDETAILS details = {0};
      MIXERCONTROLDETAILS_LISTTEXT *text = nullptr;
      MMRESULT r = 0;

      if (idx < 0 || idx >= controls.size())
         ERROR_SET(err, unknown, "Invalid index");

      switch (controls[idx].type)
      {
      case MIXERCONTROL_CONTROLTYPE_VOLUME:
         return "vol";
      case MIXERCONTROL_CONTROLTYPE_MUTE:
         return "mute";
      }

      // XXX not tested, not exercised.

      text = (MIXERCONTROLDETAILS_LISTTEXT*)_alloca(sizeof(*text) * MAX(1, controls[idx].multiple));

      details.cbStruct = sizeof(details);
      details.dwControlID = controls[idx].control;
      details.cChannels = controls[idx].channels;
      details.cMultipleItems = controls[idx].multiple;
      details.cbDetails = sizeof(*text);
      details.paDetails = text;
      r = mixerGetControlDetails(
         (HMIXEROBJ)mixer,
         &details,
         MIXER_GETCONTROLDETAILSF_LISTTEXT | MIXER_OBJECTF_HMIXER
      );
      if (r)
         ERROR_SET(err, winmm, r);
      free(onHeap);
      onHeap = ConvertToPstr(text->szName, err);
      ERROR_CHECK(err);
      return onHeap;
   exit:
      return nullptr;
   }

   int
   GetChannels(int idx, error *err)
   {
      int r = 0;
      if (idx < 0 || idx >= controls.size())
         ERROR_SET(err, unknown, "Invalid index");
      r = controls[idx].channels;
   exit:
      return r;
   }

   void
   GetRange(int idx, value_t &min, value_t &max, error *err)
   {
      if (idx < 0 || idx >= controls.size())
         ERROR_SET(err, unknown, "Invalid index");
      min = 0;
      max = 65535;
   exit:;
   }

   void
   SetValue(int idx, const value_t *value, int n, error *err)
   {
      MIXERCONTROLDETAILS details = {0};
      MMRESULT r = 0;

      if (idx < 0 || idx >= controls.size())
         ERROR_SET(err, unknown, "Invalid index");

      details.cbStruct = sizeof(details);
      details.dwControlID = controls[idx].control;
      details.cChannels = n;
      details.cMultipleItems = controls[idx].multiple;
      details.cbDetails = sizeof(*value);
      details.paDetails = (void*)value;
      r = mixerSetControlDetails(
         (HMIXEROBJ)mixer,
         &details,
         MIXER_SETCONTROLDETAILSF_VALUE | MIXER_OBJECTF_HMIXER
      );
      if (r)
         ERROR_SET(err, winmm, r);
   exit:;
   }

   int
   GetValue(int idx, value_t *value, int n, error *err)
   {
      MIXERCONTROLDETAILS details = {0};
      MMRESULT r = 0;

      if (idx < 0 || idx >= controls.size())
         ERROR_SET(err, unknown, "Invalid index");

      details.cbStruct = sizeof(details);
      details.dwControlID = controls[idx].control;
      details.cChannels = n;
      details.cMultipleItems = controls[idx].multiple;
      details.cbDetails = sizeof(*value);
      details.paDetails = value;
      r = mixerGetControlDetails(
         (HMIXEROBJ)mixer,
         &details,
         MIXER_GETCONTROLDETAILSF_VALUE | MIXER_OBJECTF_HMIXER
      );
      if (r)
         ERROR_SET(err, winmm, r);
      return details.cChannels;
   exit:
      return 0;
   }

   MuteState
   GetMuteState(int idx, error *err)
   {
      MuteState r = MuteState::None;
      auto muteControl = muteControls.find(idx);

      if (muteControl != muteControls.end())
      {
         r |= MuteState::CanMute;

         MIXERCONTROLDETAILS details = {0};
         MMRESULT mmr = 0;
         DWORD muted = 0;

         details.cbStruct = sizeof(details);
         details.dwControlID = muteControl->second.control;
         details.cChannels = 1;
         details.cbDetails = sizeof(muted);
         details.paDetails = &muted;
         mmr = mixerGetControlDetails(
            (HMIXEROBJ)mixer,
            &details,
            MIXER_GETCONTROLDETAILSF_VALUE | MIXER_OBJECTF_HMIXER
         );
         if (mmr)
            ERROR_SET(err, winmm, mmr);

         if (muted)
            r |= MuteState::Muted;
      }
      else if (idx < 0 || idx >= controls.size())
         ERROR_SET(err, unknown, "Invalid index");

   exit:
      return r;
   }

   void
   SetMute(int idx, bool on, error *err)
   {
      auto muteControl = muteControls.find(idx);

      if (muteControl != muteControls.end())
      {
         MIXERCONTROLDETAILS details = {0};
         MMRESULT mmr = 0;
         DWORD muted = on ? 1 : 0;

         details.cbStruct = sizeof(details);
         details.dwControlID = muteControl->second.control;
         details.cChannels = 1;
         details.cbDetails = sizeof(muted);
         details.paDetails = &muted;
         mmr = mixerSetControlDetails(
            (HMIXEROBJ)mixer,
            &details,
            MIXER_SETCONTROLDETAILSF_VALUE | MIXER_OBJECTF_HMIXER
         );
         if (mmr)
            ERROR_SET(err, winmm, mmr);
      }
      else if (idx < 0 || idx >= controls.size())
         ERROR_SET(err, unknown, "Invalid index");

   exit:;
   }
};

struct WinMmEnumerator : public DeviceEnumerator
{
   void
   CreateDevice(UINT_PTR deviceId, WinMmDev **out, error *err)
   {
      MMRESULT res = 0;
      WAVEOUTCAPS caps = {0};
      Pointer<WinMmDev> dev;
      res = waveOutGetDevCaps(deviceId, &caps, sizeof(caps));
      if (res) ERROR_SET(err, winmm, res);
      try
      {
         *dev.GetAddressOf() = new WinMmDev(deviceId, caps.szPname);
      }
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }
   exit:
      *out = dev.Detach();
   }

   void
   CreateDevice(UINT_PTR deviceId, Device **out, error *err)
   {
      WinMmDev *dev = nullptr;
      CreateDevice(deviceId, &dev, err);
      *out = dev;
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

   void
   GetDefaultMixer(struct Mixer **out, error *err)
   {
      Pointer<WinMmMixer> r;
      Pointer<WinMmDev> dev;
      Metadata md;

      // Need to create a handle for WAVE_MAPPER.
      //
      CreateDevice(WAVE_MAPPER, dev.GetAddressOf(), err);
      ERROR_CHECK(err);
      md.Channels = 2;
      md.Format = PcmShort;
      md.SampleRate = 44100;
      dev->ProbeSampleRate(md.SampleRate, md.SampleRate, err);
      ERROR_CHECK(err);
      md.SamplesPerFrame = 20 * md.SampleRate / 1000;
      dev->SetMetadata(md, err);
      ERROR_CHECK(err);

      New(r, err);
      ERROR_CHECK(err);

      r->Initialize((UINT)dev->GetHandle(), MIXER_OBJECTF_HWAVEOUT, err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err))
         r = nullptr;
      *out = r.Detach();
   }

   void
   GetMixer(int i, struct Mixer **out, error *err)
   {
      Pointer<WinMmMixer> r;

      New(r, err);
      ERROR_CHECK(err);

      r->Initialize(i, MIXER_OBJECTF_WAVEOUT, err);
      ERROR_CHECK(err);

   exit:
      if (ERROR_FAILED(err))
         r = nullptr;
      *out = r.Detach();
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
