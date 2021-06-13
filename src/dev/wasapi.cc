/*
 Copyright (C) 2017-2018, 2020-2021 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <windows.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <mmdeviceapi.h>
#include <AudioClient.h>
#include <AudioSessionTypes.h>
#include <EndpointVolume.h>

#include <common/misc.h>
#include <common/logger.h>
#include <common/c++/new.h>
#include <AudioDevice.h>

#include <string.h>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM      0x80000000U
#endif

#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000U
#endif

#include "win.h"

#include <initguid.h>
#include <devpkey.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

using namespace Microsoft::WRL;
using namespace common;
using namespace audio;
using namespace audio::windows;

namespace {

void
CreateDefaultDeviceMonitor(
   IMMDeviceEnumerator *devEnum,
   const std::function<void(EDataFlow, ERole, PCWSTR, error *)> &callback,
   IUnknown **out,
   error *err
);

class WasapiDev : public Device
{
   PSTR devName;
   ComPtr<IMMDevice> dev;
   ComPtr<IAudioClient> client;
   ComPtr<IAudioRenderClient> renderClient;
   bool started;
   int blockAlign;
   HANDLE event;
   ComPtr<IUnknown> defaultDevMonitor;
   bool deviceChanged;

   void
   CleanupOld(void)
   {
      if (client.Get())
      {
         if (started)
            client->Stop();
         started = false;
         client = nullptr;
         renderClient = nullptr;
      }
   }

public:

   WasapiDev(IMMDevice *dev_)
      : devName(nullptr), dev(dev_), started(false), event(nullptr), deviceChanged(false) {}

   ~WasapiDev()
   {
      CleanupOld();
      if (event)
         CloseHandle(event);
      free(devName);
   }

   void
   Initialize(IMMDeviceEnumerator *devEnum, bool isDefault, error *err)
   {
      if (isDefault)
      {
         Pointer<WasapiDev> rc = this;
         auto weak = rc.MakeWeak(err);
         ERROR_CHECK(err);

         CreateDefaultDeviceMonitor(
            devEnum,
            [weak] (EDataFlow flow, ERole role, PCWSTR devName, error *err) -> void
            {
               auto rc = weak.Lock();
               if (rc.Get() && flow == eRender && role == eMultimedia)
                  rc->deviceChanged = true;
            },
            defaultDevMonitor.ReleaseAndGetAddressOf(),
            err
         );
      }
      else
      {
         defaultDevMonitor = nullptr;
      }

      event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
      if (!event)
         ERROR_SET(err, win32, GetLastError());
   exit:;
   }

   void
   Activate(error *err)
   {
      HRESULT hr = S_OK;

      hr = dev->Activate(
         __uuidof(IAudioClient),
         CLSCTX_ALL,
         nullptr,
         (PVOID*)client.GetAddressOf()
      );
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

   exit:;
   }

   void
   SetMetadata(const Metadata &metadata, error *err)
   {
      HRESULT hr = S_OK;
      WAVEFORMATEX fmt;
      int attempts = 5;

      CleanupOld();

      Activate(err);
      ERROR_CHECK(err);

      MetadataToWaveFormatEx(metadata, &fmt);
      blockAlign = fmt.nBlockAlign;

      WaitForSingleObject(event, 0);

retry:
      hr = client->Initialize(
         AUDCLNT_SHAREMODE_SHARED,
         AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
         metadata.SamplesPerFrame * 10000000LL /
            metadata.SampleRate,
         0,
         &fmt,
         nullptr
      );
      if (FAILED(hr) && attempts--)
      {
         error_set_win32(err, hr);
         ERROR_LOG(err);
         log_printf("Open failed, retrying...");
         error_clear(err);
         goto retry;
      }
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      hr = client->SetEventHandle(event);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      hr = client->GetService(IID_PPV_ARGS(renderClient.GetAddressOf()));
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

   exit:
      ;
   }

   void
   Write(const void *buf, int len, error *err)
   {
      HRESULT hr = S_OK;
      PBYTE driverBuffer = nullptr;
      UINT32 bufferSize = 0, padding = 0;
      int n, m;

      if (deviceChanged)
         ERROR_SET(err, unknown, "Default device changed");

   retry:
      hr = client->GetBufferSize(&bufferSize);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      hr = client->GetCurrentPadding(&padding);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      bufferSize -= padding;

      n = len / blockAlign;
      m = MIN(n, bufferSize);

      hr = renderClient->GetBuffer(m, &driverBuffer);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      memcpy(driverBuffer, buf, m * blockAlign);
      buf = ((const char*)buf) + m * blockAlign;
      len -= m * blockAlign;

      renderClient->ReleaseBuffer(m, 0);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      if (!started)
      {
         hr = client->Start();
         if (FAILED(hr))
            ERROR_SET(err, win32, hr);
         started = true;
      }

      if (len)
      {
         WaitForSingleObject(event, INFINITE);
         goto retry;
      }
   exit:;
   }

   const char *
   GetName(error *err)
   {
      HRESULT hr = S_OK;
      ComPtr<IPropertyStore> props;
      PROPVARIANT prop;
      PropVariantInit(&prop);

      if (devName)
         goto exit;

      hr = dev->OpenPropertyStore(STGM_READ, props.GetAddressOf());
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      hr = props->GetValue(*(PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &prop);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      devName = ConvertToPstr(prop.pwszVal, err);
      ERROR_CHECK(err);

   exit:
      PropVariantClear(&prop);
      return devName;
   }

   void
   ProbeSampleRate(int rate, int &suggested, error *err)
   {
      HRESULT hr = S_OK;
      Metadata md;
      WAVEFORMATEX inFmt;
      WAVEFORMATEX *outFmt = nullptr;

      if (!client.Get())
      {
         Activate(err);
         ERROR_CHECK(err);
      }

      md.Format = PcmShort;
      md.Channels = 2;
      md.SampleRate = rate;

      MetadataToWaveFormatEx(md, &inFmt);

      hr = client->IsFormatSupported(
         AUDCLNT_SHAREMODE_SHARED,
         &inFmt,
         &outFmt
      );
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      if (outFmt)
         suggested = outFmt->nSamplesPerSec;
   exit:
      if (outFmt)
         CoTaskMemFree(outFmt);
   }
};

class WasapiMixer : public Mixer
{
   ComPtr<IMMDevice> dev;
   ComPtr<IAudioEndpointVolume> volume;
   ComPtr<IUnknown> defaultDevMonitor;
public:

   WasapiMixer(IMMDevice *dev_)
      : dev(dev_) {}

   void
   Initialize(IMMDeviceEnumerator *devEnum_, bool isDefault, error *err)
   {
      if (isDefault)
      {
         Pointer<WasapiMixer> rc = this;
         auto weak = rc.MakeWeak(err);
         ERROR_CHECK(err);

         ComPtr<IMMDeviceEnumerator> devEnum = devEnum_;

         CreateDefaultDeviceMonitor(
            devEnum.Get(),
            [devEnum, weak] (EDataFlow flow, ERole role, PCWSTR devName, error *err) -> void
            {
               auto rc = weak.Lock();
               if (!rc.Get())
                  return;
               if (flow == eRender && role == eMultimedia)
               {
                  HRESULT hr = S_OK;

                  hr = devEnum->GetDefaultAudioEndpoint(
                     eRender,
                     eMultimedia,
                     rc->dev.ReleaseAndGetAddressOf()
                  );
                  if (FAILED(hr))
                     ERROR_SET(err, win32, hr);
                  rc->Initialize(devEnum.Get(), false, err);
                  ERROR_CHECK(err);
               }
            exit:;
            },
            defaultDevMonitor.GetAddressOf(),
            err
         );
      }

      Activate(err);
      ERROR_CHECK(err);
   exit:;
   }

   void
   Activate(error *err)
   {
      HRESULT hr = S_OK;

      hr = dev->Activate(
         __uuidof(IAudioEndpointVolume),
         CLSCTX_ALL,
         nullptr,
         (PVOID*)volume.ReleaseAndGetAddressOf()
      );
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);
   exit:;
   }

   int
   GetValueCount(error *err)
   {
      return 1;
   }

   const char *
   DescribeValue(int idx, error *err)
   {
      switch (idx)
      {
      case 0:  return "vol";
      default: ERROR_SET(err, unknown, "Invalid index");
      }

   exit:
      return nullptr;
   }

   int
   GetChannels(int idx, error *err)
   {
      UINT r = 0;
      HRESULT hr = S_OK;

      DescribeValue(idx, err);
      ERROR_CHECK(err);

      if (volume.Get() == nullptr)
         ERROR_SET(err, win32, E_POINTER);

      hr = volume->GetChannelCount(&r);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

   exit:
      return r;
   }

   void
   SetValue(int idx, const float *value, int n, error *err)
   {
      DescribeValue(idx, err);
      ERROR_CHECK(err);

      if (volume.Get() == nullptr)
         ERROR_SET(err, win32, E_POINTER);

      for (int i=0; i<n; ++i)
      {
         HRESULT hr = volume->SetChannelVolumeLevelScalar(i, value[i], nullptr);
         if (ERROR_FAILED(err))
            ERROR_SET(err, win32, hr);
      }

   exit:;
   }

   int
   GetValue(int idx, float *value, int n, error *err)
   {
      int r = 0;

      DescribeValue(idx, err);
      ERROR_CHECK(err);

      if (volume.Get() == nullptr)
         ERROR_SET(err, win32, E_POINTER);

      for (int i=0; i<n; ++i)
      {
         HRESULT hr = volume->GetChannelVolumeLevelScalar(i, &value[i]);
         if (ERROR_FAILED(err))
            ERROR_SET(err, win32, hr);
      }
      r = n;

   exit:
      return r;
   }

   MuteState
   GetMuteState(int idx, error *err)
   {
      MuteState r = MuteState::None;
      HRESULT hr = S_OK;
      BOOL muted = FALSE;

      DescribeValue(idx, err);
      ERROR_CHECK(err);

      if (volume.Get() == nullptr)
         ERROR_SET(err, win32, E_POINTER);

      hr = volume->GetMute(&muted);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      r = MuteState::CanMute | (muted ? MuteState::Muted : MuteState::None);
   exit:
      return r;
   }

   void
   SetMute(int idx, bool on, error *err)
   {
      HRESULT hr = S_OK;

      DescribeValue(idx, err);
      ERROR_CHECK(err);

      if (volume.Get() == nullptr)
         ERROR_SET(err, win32, E_POINTER);

      hr = volume->SetMute(on ? TRUE : FALSE, nullptr);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);
   exit:;
   }
};

class WasapiEnumerator : public DeviceEnumerator
{
   ComPtr<IMMDeviceEnumerator> devEnum;
   ComPtr<IMMDeviceCollection> devs;
public:

   void
   Initialize(error *err)
   {
      HRESULT hr = S_OK;

      hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      hr = CoCreateInstance(
         __uuidof(MMDeviceEnumerator),
         nullptr,
         CLSCTX_INPROC_SERVER,
         IID_PPV_ARGS(devEnum.GetAddressOf())
      );
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);
   exit:;
   }

   int
   GetDeviceCount(error *err)
   {
      HRESULT hr = S_OK;
      UINT count = 0;

      if (!devs.Get())
      {
         hr = devEnum->EnumAudioEndpoints(
            eRender,
            DEVICE_STATE_ACTIVE,
            devs.GetAddressOf()
         );
         if (FAILED(hr))
            ERROR_SET(err, win32, hr);
      }

      hr = devs->GetCount(&count);
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

   exit:
      return count;
   }

   #define WrapFn(FN, TYPE) \
      [this] (IMMDevice *a, bool b, TYPE **c, error *d) -> void { FN(a,b,c,d); }

   void
   GetDevice(int i, Device **out, error *err)
   {
      GetDevice(i, WrapFn(CreateDevice, Device), out, err);
   }

   void
   GetDefaultDevice(Device **out, error *err)
   {
      GetDefaultDevice(WrapFn(CreateDevice, Device), out, err);
   }

   void
   GetMixer(int i, struct Mixer **out, error *err)
   {
      GetDevice(i, WrapFn(CreateDevice, struct Mixer), out, err);
   }

   void
   GetDefaultMixer(struct Mixer **out, error *err)
   {
      GetDefaultDevice(WrapFn(CreateDevice, struct Mixer), out, err);
   }

   #undef WrapFn

private:

   template<typename T, typename Creator>
   void
   GetDevice(int i, const Creator &creator, T **out, error *err)
   {
      HRESULT hr = S_OK;
      ComPtr<IMMDevice> dev;

      auto count = GetDeviceCount(err);
      ERROR_CHECK(err);

      if (i < 0 || i >= count)
         ERROR_SET(err, win32, E_INVALIDARG);

      hr = devs->Item(i, dev.GetAddressOf());
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      creator(dev.Get(), false, out, err);
      ERROR_CHECK(err);
   exit:;
   }

   template<typename T, typename Creator>
   void
   GetDefaultDevice(const Creator &creator, T **out, error *err)
   {
      HRESULT hr = S_OK;
      ComPtr<IMMDevice> dev;

      hr = devEnum->GetDefaultAudioEndpoint(
         eRender,
         eMultimedia,
         dev.GetAddressOf()
      );
      if (FAILED(hr))
         ERROR_SET(err, win32, hr);

      creator(dev.Get(), true, out, err);
      ERROR_CHECK(err);
   exit:;
   }

   void
   CreateDevice(IMMDevice *dev, bool isDefault, Device **out, error *err)
   {
      Pointer<WasapiDev> r;
      try
      {
         *r.GetAddressOf() = new WasapiDev(dev);
      }
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }
      r->Initialize(devEnum.Get(), isDefault, err);
      ERROR_CHECK(err);
   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *out = r.Detach();
   }

   void
   CreateDevice(IMMDevice *dev, bool isDefault, struct Mixer **out, error *err)
   {
      Pointer<WasapiMixer> r;
      try
      {
         *r.GetAddressOf() = new WasapiMixer(dev);
      }
      catch (const std::bad_alloc&)
      {
         ERROR_SET(err, nomem);
      }
      r->Initialize(devEnum.Get(), isDefault, err);
      ERROR_CHECK(err);
   exit:
      if (ERROR_FAILED(err)) r = nullptr;
      *out = r.Detach();
   }
};

struct DefaultDeviceMonitor : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMMNotificationClient>
{
   std::function<void(EDataFlow, ERole, PCWSTR, error *)> callback;

   HRESULT CALLBACK
   OnDefaultDeviceChanged(
      EDataFlow flow,
      ERole     role,
      PCWSTR    name
   )
   {
      error err;
      callback(flow, role, name, &err);
      return ERROR_FAILED(&err) ? E_FAIL : S_OK;
   }

   HRESULT CALLBACK OnDeviceAdded(PCWSTR name) { return S_OK; }
   HRESULT CALLBACK OnDeviceRemoved(PCWSTR name) { return S_OK; }
   HRESULT CALLBACK OnDeviceStateChanged(PCWSTR name, DWORD state) { return S_OK; }
   HRESULT CALLBACK OnPropertyValueChanged(PCWSTR name, const PROPERTYKEY key) { return S_OK; }
};

struct DeviceMonitorRaii : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUnknown>
{
   ComPtr<IMMDeviceEnumerator> devEnum;
   ComPtr<IMMNotificationClient> notification;

   ~DeviceMonitorRaii()
   {
      if (devEnum.Get() && notification.Get())
        devEnum->UnregisterEndpointNotificationCallback(notification.Get());
   }
};

void
CreateDefaultDeviceMonitor(
   IMMDeviceEnumerator *devEnum,
   const std::function<void(EDataFlow, ERole, PCWSTR, error *)> &callback,
   IUnknown **out,
   error *err
)
{
   HRESULT hr = S_OK;
   ComPtr<DefaultDeviceMonitor> mon;
   ComPtr<DeviceMonitorRaii> raii;

   hr = MakeAndInitialize<DefaultDeviceMonitor>(mon.GetAddressOf());
   if (FAILED(hr))
      ERROR_SET(err, win32, hr);

   mon->callback = callback;

   hr = MakeAndInitialize<DeviceMonitorRaii>(raii.GetAddressOf());
   if (FAILED(hr))
      ERROR_SET(err, win32, hr);

   hr = devEnum->RegisterEndpointNotificationCallback(mon.Get());
   if (FAILED(hr))
      ERROR_SET(err, win32, hr);

   raii->devEnum = devEnum;
   raii->notification = mon;

   *out = raii.Detach();
exit:;
}

} // namespace

void
audio::GetWasapiDeviceEnumerator(DeviceEnumerator **out, error *err)
{
   Pointer<WasapiEnumerator> r;

   New(r.GetAddressOf(), err);
   ERROR_CHECK(err);

   r->Initialize(err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err)) r = nullptr;
   *out = r.Detach();
}
