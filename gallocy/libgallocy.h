#ifndef _LIBGALLOCY_H
#define _LIBGALLOCY_H

#include "pagetable.h"
#include "firstfitheap.h"

#include "heaplayers/heaptypes.h"


class MainHeap: public MainHeapType {};


extern MainHeap heap;


extern SingletonHeapType singletonHeap;


extern "C" {

  void __reset_memory_allocator();

  void* custom_malloc(size_t);
  void custom_free(void*);
  void* custom_realloc(void*, size_t);

  // This is an OSX thing, but is useful to provide for testing...
  size_t custom_malloc_usable_size(void*);

#ifdef __APPLE__

  void custom_malloc_lock();
  void custom_malloc_unlock();

#endif

}

#endif
