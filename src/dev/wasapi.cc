/*
 Copyright (C) 2017, 2018 Andrew Sveikauskas

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

   void CleanupOld(void)
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

   void Initialize(IMMDeviceEnumerator *devEnum, bool isDefault, error *err)
   {
      if (isDefault)
      {
         CreateDefaultDeviceMonitor(
            devEnum,
            [this] (EDataFlow flow, ERole role, PCWSTR devName, error *err) -> void
            {
               if (flow == eRender && role == eMultimedia)
                  deviceChanged = true;
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

   void Activate(error *err)
   {
      HRESULT hr = S_OK;

      hr = dev->Activate(
         __uuidof(IAudioClient),
         CLSCTX_ALL,
         nullptr,
         (PVOID*)client.GetAddressOf()
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

   exit:;
   }

   void SetMetadata(const Metadata &metadata, error *err)
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
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = client->SetEventHandle(event);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = client->GetService(IID_PPV_ARGS(renderClient.GetAddressOf()));
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

   exit:
      ;
   }

   void Write(const void *buf, int len, error *err)
   {
      HRESULT hr = S_OK;
      PBYTE driverBuffer = nullptr;
      UINT32 bufferSize = 0, padding = 0;
      int n, m;

      if (deviceChanged)
         ERROR_SET(err, unknown, "Default device changed");

   retry:
      hr = client->GetBufferSize(&bufferSize);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      hr = client->GetCurrentPadding(&padding);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      bufferSize -= padding;

      n = len / blockAlign;
      m = MIN(n, bufferSize);

      hr = renderClient->GetBuffer(m, &driverBuffer);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      memcpy(driverBuffer, buf, m * blockAlign);
      buf = ((const char*)buf) + m * blockAlign;
      len -= m * blockAlign;

      renderClient->ReleaseBuffer(m, 0);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      if (!started)
      {
         hr = client->Start();
         if (FAILED(hr)) ERROR_SET(err, win32, hr);
         started = true;
      }

      if (len)
      {
         WaitForSingleObject(event, INFINITE);
         goto retry;
      }
   exit: 
      ;
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
      if (FAILED(hr)) ERROR_SET(err, win32, hr);
   
      hr = props->GetValue(*(PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &prop);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);
   
      devName = ConvertToPstr(prop.pwszVal, err);
      ERROR_CHECK(err);
   
   exit:
      PropVariantClear(&prop);
      return devName;
   }

   void ProbeSampleRate(int rate, int &suggested, error *err)
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
      if (FAILED(hr)) ERROR_SET(err, win32, hr);

      if (outFmt)
         suggested = outFmt->nSamplesPerSec;
   exit:
      if (outFmt)
         CoTaskMemFree(outFmt);
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
      if (FAILED(hr)) ERROR_SET(err, win32, hr);
   
      hr = CoCreateInstance(
         __uuidof(MMDeviceEnumerator),
         nullptr,
         CLSCTX_INPROC_SERVER,
         IID_PPV_ARGS(devEnum.GetAddressOf())
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);
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
         if (FAILED(hr)) ERROR_SET(err, win32, hr);
      }
   
      hr = devs->GetCount(&count);
      if (FAILED(hr)) ERROR_SET(err, win32, hr);
   
   exit:
      return count;
   }
   
   void
   GetDevice(int i, Device **out, error *err)
   {
      HRESULT hr = S_OK;
      ComPtr<IMMDevice> dev;
   
      auto count = GetDeviceCount(err);
      ERROR_CHECK(err);
   
      if (i < 0 || i >= count)
         ERROR_SET(err, win32, E_INVALIDARG);
   
      hr = devs->Item(i, dev.GetAddressOf()); 
      if (FAILED(hr)) ERROR_SET(err, win32, hr);
   
      CreateDevice(dev.Get(), false, out, err);
      ERROR_CHECK(err);
   exit:;
   }
   
   void
   GetDefaultDevice(Device **out, error *err)
   {
      HRESULT hr = S_OK;
      ComPtr<IMMDevice> dev;
   
      hr = devEnum->GetDefaultAudioEndpoint(
         eRender,
         eMultimedia,
         dev.GetAddressOf()
      );
      if (FAILED(hr)) ERROR_SET(err, win32, hr);
   
      CreateDevice(dev.Get(), true, out, err);
      ERROR_CHECK(err);
   exit:;
   }
   
private:
   void CreateDevice(IMMDevice *dev, bool isDefault, Device **out, error *err)
   {
      Pointer<WasapiDev> r;
      try
      {
         *r.GetAddressOf() = new WasapiDev(dev);
      }
      catch (std::bad_alloc)
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
   if (FAILED(hr)) ERROR_SET(err, win32, hr);

   mon->callback = callback;

   hr = MakeAndInitialize<DeviceMonitorRaii>(raii.GetAddressOf());
   if (FAILED(hr)) ERROR_SET(err, win32, hr);

   hr = devEnum->RegisterEndpointNotificationCallback(mon.Get());
   if (FAILED(hr)) ERROR_SET(err, win32, hr);

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
