/*
 Copyright (C) 2020 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <common/misc.h>

//
// A slightly hacky class to use arrays for the first <treshold> items,
// and move to the heap for a larger buffer.
//

namespace {

template<typename T, size_t threshold>
class StackArray
{
   union
   {
      T arr[threshold];
      T *ptr;
   };
   size_t size;

   void
   free()
   {
      if (size > threshold)
      {
         if (ptr)
         {
            delete [] ptr;
            ptr = nullptr;
         }
      }
      else
      {
         for (size_t i=0; i<size; ++i)
         {
            arr[i].~T();
         }
      }
      size = 0;
   }

public:
   StackArray() : size(0) {}

   StackArray(size_t size_)
      : size(0)
   {
      if (size_ > threshold)
      {
         ptr = new T[size = size_];
      }
      else
      {
         for (size=0; size<size_; ++size)
         {
            try
            {
               new (&arr[size]) T();
            }
            catch (std::bad_alloc)
            {
               for (auto i = 0; i<size; ++i)
                  arr[i].~T();

               throw;
            }
         }
      }
   }

   StackArray(const StackArray &o)
      : size(o.size)
   {
      if (size > threshold)
      {
         ptr = new T[size];

         for (size_t i=0; i<size; ++i)
         {
            try
            {
               ptr[i] = o.ptr[i];
            }
            catch (std::bad_alloc)
            {
               delete [] ptr;
               ptr = nullptr;
               throw;
            }
         }
      }
      else
      {
         for (size_t i=0; i<size; ++i)
         {
            try
            {
               new (&arr[i]) T(o.arr[i]);
            }
            catch (std::bad_alloc)
            {
               for (auto i = 0; i<size; ++i)
                  arr[i].~T();

               throw;
            }
         }
      }
   }

   StackArray(StackArray &&o)
      : size(0)
   {
      *this = std::move(o);
   }

   ~StackArray()
   {
      free();
   }

   T*
   begin()
   {
      return size > threshold ? ptr : arr;
   }

   T*
   end()
   {
      return begin() + size;
   }

   void
   resize(size_t n, error *err)
   {
      // Don't re-allocate if we're just going to shrink.
      //
      if (n <= size)
      {
         if (n < threshold)
         {
            for (auto m=n; m<size; ++m)
               arr[m].~T();
         }
         size = n;
         return;
      }

      try
      {
         StackArray other(n);
         auto dst = other.begin();
         auto src = this->begin();
         auto end = src + MIN(size, n);

         while (src < end)
            *dst++ = std::move(*src++);

         *this = std::move(other);
      }
      catch (std::bad_alloc)
      {
         error_set_nomem(err);
      }
   }

   StackArray &
   operator = (const StackArray &other)
   {
      if (this == &other)
         return *this;
      StackArray cp(other);
      return *this = std::move(cp);
   }

   StackArray &
   operator = (StackArray &&o)
   {
      if (this == &o)
         return *this;

      free();
      size = o.size;

      if (size > threshold)
      {
         ptr = o.ptr;
         o.ptr = nullptr;
      }
      else
      {
         for (size_t i = 0; i<size; ++i)
         {
            new (&arr[i]) T(std::move(o.arr[i]));
            o.arr[i].~T();
         }
      }
      o.size = 0;
      return *this;
   }
};

} // end namespace

