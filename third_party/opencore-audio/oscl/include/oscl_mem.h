/* ------------------------------------------------------------------
 * Copyright (C) 2009 Martin Storsjo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */

#ifndef OSCL_MEM_H
#define OSCL_MEM_H

#ifdef __cplusplus
#include <new>
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#define oscl_malloc malloc
#define oscl_free free
#define oscl_memset memset
#define oscl_memmove memmove
#define oscl_memcpy memcpy

#define OSCL_ARRAY_NEW(TYPE, N) \
   new (std::nothrow) TYPE[N]

#define OSCL_ARRAY_DELETE(PTR) \
   do                          \
   {                           \
      auto ptr_ = (PTR);       \
      if (ptr_)                \
         delete [] ptr_;       \
   } while (0)

#ifdef __cplusplus
}
#endif

#endif
