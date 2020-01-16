/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

//
// This file is for enumerating device nodes under /dev, as in audio or
// dsp devices .
//

#include <dirent.h>
#include <common/size.h>
#include <common/trie.h>
#include <memory>

namespace {

bool
check_atoi(const char *p, int &o)
{
   char *q = nullptr;
   o = -1;
   auto r = strtol(p, &q, 10);
   if (p != q)
   {
      o = r;
      return true;
   }
   return false;
}

class DevNodeEnumerator : public DeviceEnumerator
{
protected:
   enum Mode
   {
      Pcm                 = 0,
      Mixer               = 1,
      ConsiderEnvironment = (1 << 8),
      Flags               = ConsiderEnvironment,
   };

   // Implementation should set to TRUE to call open(2) with O_NONBLOCK.
   //
   bool openNonBlock;

   virtual
   const char * const*
   GetPossibleSubdirectories()
   {
      static const char * const r[] =
      {
         nullptr,
      };
      return r;
   }

   virtual
   const char * const*
   GetPossibleDeviceNodeNames(Mode m) = 0;

   virtual
   void
   Open(const char *filename, int fd, Device **out, error *err) = 0;

   virtual
   void
   Open(const char *filename, int fd, struct Mixer **out, error *err)
   {
      error_set_errno(err, ENOSYS);
   }

private:
   std::vector<char> possibleDefaults;
   std::vector<const char *> possibleDefaultsPtr;
   Mode lastRequestedMode;

   void
   build_device_trie(trie **devs, Mode m, error *err)
   {
      for (auto dev = GetPossibleDeviceNodeNames(m); *dev; ++dev)
      {
         trie_insert(devs, *dev, strlen(*dev), (void*)*dev, nullptr, err);
         ERROR_CHECK(err);
      }
   exit:;
   }

   std::unique_ptr<const char, void (*)(const char*)>
   GetPcmFromEnvironment()
   {
      return MakeNonDestructableString(getenv("AUDIODEV"));
   }

   std::unique_ptr<const char, void (*)(const char*)>
   GetMixerFromEnvironment(error *err)
   {
      const char *env = getenv("MIXERDEV");
      if (env)
         return MakeNonDestructableString(env);
      auto dev = GetPcmFromEnvironment();
      if (dev.get())
      {
         auto r = TryGetMixerFromPcmPath(dev.get(), err);
         ERROR_CHECK(err);
         return r;
      }
   exit:
      return MakeNonDestructableString(nullptr);
   }

   std::unique_ptr<const char, void (*)(const char*)>
   TryGetMixerFromPcmPath(const char *dev, error *err)
   {
      const char *filePart = nullptr;
      trie *devs = nullptr;
      build_device_trie(&devs, Pcm, err);
      std::unique_ptr<trie, void (*)(trie*)> trieSp(devs, trie_free);
      size_t len;
      size_t prefixLen;

      ERROR_CHECK(err);

      filePart = strrchr(dev, '/');
      filePart = filePart ? (filePart+1) : dev;
      len = strlen(filePart);
      prefixLen = trie_get_prefix_length(devs, filePart, len);

      if (prefixLen)
      {
         auto mixerName = GetPossibleDeviceNodeNames(Mixer);
         char buf[128] = {0};
         size_t mixerLen = 0;
         char *onHeap = nullptr;
         size_t heapLen = 0;

         if (!mixerName || !*mixerName)
            goto exit;

         strncpy(buf, *mixerName, sizeof(buf)-1);
         mixerLen = strlen(buf);

         if (filePart[prefixLen])
         {
            int i;

            if (check_atoi(filePart + prefixLen, i))
            {
               snprintf(buf + mixerLen, sizeof(buf)-mixerLen, "%d", i);
               mixerLen = strlen(buf);
            }
         }

         heapLen = (filePart - dev) + 1;
         if (size_add(heapLen, mixerLen, &heapLen))
            ERROR_SET(err, unknown, "Integer overflow");
         ERROR_CHECK(err);
         onHeap = (char*)malloc(heapLen);
         if (!onHeap)
            ERROR_SET(err, nomem);
         auto r = MakeHeapString(onHeap);
         memcpy(onHeap, dev, (filePart - dev));
         memcpy(onHeap+(filePart - dev), buf, mixerLen+1);
         return r;
      }

   exit:
      return MakeNonDestructableString(nullptr);
   }

   const char *const*
   GetPossibleDefaultDevices(Mode m, error *err)
   {
      if (possibleDefaults.size() && m == lastRequestedMode)
         return possibleDefaultsPtr.data();
      possibleDefaults.resize(0);
      possibleDefaultsPtr.resize(0);
      lastRequestedMode = m;
      try
      {
         auto addString = [&] (const char *p, bool null) -> void
         {
            possibleDefaults.insert(possibleDefaults.end(), p, p+strlen(p));
            if (null)
               possibleDefaults.push_back(0);
         };

         if (m & ConsiderEnvironment)
         {
            auto env = MakeNonDestructableString(nullptr);

            switch ((Mode)(m & ~Flags))
            {
            case Pcm:
               env = GetPcmFromEnvironment();
               break;
            case Mixer:
               env = GetMixerFromEnvironment(err);
               ERROR_CHECK(err);
               break;
            case ConsiderEnvironment:
               break;
            }
            if (env.get())
               addString(env.get(), true);
         }

         auto subdir = GetPossibleSubdirectories();
         do
         {
            for (auto dev = GetPossibleDeviceNodeNames((Mode)(m & ~Flags)); *dev; ++dev)
            {
               addString("/dev/", false);
               addString(*subdir ? *subdir : "", false);
               if (*subdir)
                  possibleDefaults.push_back('/');
               addString(*dev, true);
            }
         } while (*subdir++);

         char *p = possibleDefaults.data();
         while (p < possibleDefaults.data()+possibleDefaults.size())
         {
            possibleDefaultsPtr.push_back(p);
            p += strlen(p)+1;
         }
         possibleDefaultsPtr.push_back(nullptr);

         return possibleDefaultsPtr.data();
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
   exit:
      possibleDefaults.resize(0);
      possibleDefaultsPtr.resize(0);
      return nullptr;
   }

   void
   Open(const char *filename, Device **out, error *err)
   {
      int fd = -1;

      fd = open(filename, (openNonBlock ? O_NONBLOCK : 0) | O_WRONLY);
      if (fd < 0)
      {
         // too noisy to log.
         //
         if (errno == ENOENT)
            goto exit;

         ERROR_SET(err, errno, errno);
      }

      if (openNonBlock &&
          fcntl(fd, F_SETFL, 0))
      {
         ERROR_SET(err, errno, errno);
      }

      Open(filename, fd, out, err);
      ERROR_CHECK(err);

      fd = -1;
   exit:
      if (fd >= 0)
         close(fd);
   }

   void
   Open(const char *filename, struct Mixer **out, error *err)
   {
      int fd = -1;

      fd = open(filename, O_RDWR);
      if (fd < 0)
      {
         // too noisy to log.
         //
         if (errno == ENOENT)
            goto exit;

         ERROR_SET(err, errno, errno);
      }

      Open(filename, fd, out, err);
      ERROR_CHECK(err);

      fd = -1;
   exit:
      if (fd >= 0)
         close(fd);
   }

public:

   int
   GetDeviceCount(error *err)
   {
      DIR *dir = nullptr;
      trie *devs = nullptr;
      auto subdir = GetPossibleSubdirectories();
      int max = -1;
      bool sawDefault = false;

      build_device_trie(&devs, Pcm, err);
      ERROR_CHECK(err);

      do
      {
         std::vector<char> str;

         try
         {
            auto addString = [&] (const char *p) -> void
            {
               str.insert(str.end(), p, p+strlen(p));
            };

            addString("/dev");
            if (*subdir)
            {
               str.push_back('/');
               addString(*subdir);
            }
            str.push_back(0);
         }
         catch (std::bad_alloc)
         {
            ERROR_SET(err, nomem);
         }

         if (dir)
            closedir(dir);
         dir = opendir(str.data());
         if (!dir)
            continue;

         struct dirent *ent;
         while ((ent = readdir(dir)))
         {
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
            auto namlen = ent->d_namlen;
#else
            auto namlen = strlen(ent->d_name);
#endif
            auto prefix = trie_get_prefix_length(devs, ent->d_name, namlen);
            if (prefix)
            {
               if (prefix == namlen)
                  sawDefault = true;
               else
               {
                  int i;
                  if (check_atoi(ent->d_name + prefix, i) && i >= max)
                  {
                     max = i;
                  }
               }
            }
         }
      } while (*subdir++);
   exit:
      if (dir)
         closedir(dir);
      trie_free(devs);
      if (ERROR_FAILED(err))
         return 0;
      return (max >= 0) ? max+1 : (sawDefault ? 1 : 0);
   }

   void
   GetDevice(int idx, Device **output, error *err)
   {
      GetDevice(idx, Pcm, output, err);
   }

   void
   GetDefaultDevice(Device **output, error *err)
   {
      GetDefaultDevice(Pcm, output, err);
   }

   void
   GetMixer(int idx, struct Mixer **output, error *err)
   {
      GetDevice(idx, Mixer, output, err);
   }

   void
   GetDefaultMixer(struct Mixer **output, error *err)
   {
      GetDefaultDevice(Mixer, output, err);
   }

   DevNodeEnumerator() : openNonBlock(false), lastRequestedMode((Mode)-1) {}

private:

   template<typename T>
   void
   GetDevice(int idx, Mode mode, T **output, error *err)
   {
      Pointer<T> r;

      if (idx < 0)
         ERROR_SET(err, errno, EINVAL);

      for (auto devNames = GetPossibleDefaultDevices(mode, err); devNames && *devNames; ++devNames)
      {
         try
         {
            char buf[64];
            std::string devName(*devNames);
            auto originalLength = devName.size();
            bool retried = false;

            snprintf(buf, sizeof(buf), "%d", idx);
            devName += buf;

            Open(devName.c_str(), r.ReleaseAndGetAddressOf(), err);
         retry:
            if (!ERROR_FAILED(err) && r.Get())
               break;
            if (idx == 0 && !retried)
            {
               error_clear(err);
               devName.resize(originalLength);
               Open(devName.c_str(), r.ReleaseAndGetAddressOf(), err);
               retried = true;
               goto retry;
            }
            else if (devNames[1])
            {
               error_clear(err);
            }
            ERROR_CHECK(err);
         }
         catch (std::bad_alloc)
         {
            ERROR_SET(err, nomem);
         }
      }
   exit:
      *output = r.Detach();
   }

   template<typename T>
   void
   GetDefaultDevice(Mode mode, T **output, error *err)
   {
      Pointer<T> r;
      auto checkError = [&] () -> void
      {
         if (ERROR_FAILED(err))
         {
            error_clear(err);
            r = nullptr;
         }
      };

      for (auto devNames = GetPossibleDefaultDevices((Mode)(mode | ConsiderEnvironment), err); devNames && *devNames; ++devNames)
      {
         Open(*devNames, r.ReleaseAndGetAddressOf(), err);
         checkError();
         if (r.Get())
            break;
      }
      // eg. on OpenBSD, the audio -> audio0 symlink isn't created for us.
      //
      if (!r.Get())
      {
         GetDevice(0, mode, r.ReleaseAndGetAddressOf(), err);
         checkError();
      }
      *output = r.Detach();
   }

   std::unique_ptr<const char, void (*)(const char*)>
   MakeNonDestructableString(const char *p)
   {
      return std::unique_ptr<const char, void (*)(const char*)>(
         p,
         [] (const char *) -> void {}
      );
   }

   std::unique_ptr<const char, void (*)(const char*)>
   MakeHeapString(const char *p)
   {
      return std::unique_ptr<const char, void (*)(const char*)>(
         p,
         [] (const char *p) -> void { free((void*)p); }
      );
   }
};

} // end namespace
