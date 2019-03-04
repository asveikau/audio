/*
 Copyright (C) 2017 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <common/error.h>
#include <windows.h>

extern "C"
{
   struct function
   {
      const char *name;
      void **address;
   };

   struct module
   {
      const char *name;
      struct function *funcs; 
   };

   void load_imports(struct module *modules, error *err);
}

void
load_imports(struct module *modules, error *err)
{
   for (; modules->name; ++modules)
   {
      HMODULE mod = LoadLibraryA(modules->name); 
      if (!mod)
         continue;
      for (auto p = modules->funcs; p->name; ++p)
      {
         FARPROC q = GetProcAddress(mod, p->name);
         if (q)
            *p->address = q;
      }
   }
}
