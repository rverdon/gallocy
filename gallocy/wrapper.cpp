#include <errno.h>

#include <cstring>
#include <cstdlib>
#include <cstdio>

#if __linux__
#include <malloc.h>
#ifndef __MALLOC_HOOK_VOLATILE
#define __MALLOC_HOOK_VOLATILE
#endif

extern "C" {

  // Declare symbols, but define them somewhere else
  void* custom_malloc(size_t sz);
  void custom_free(void* ptr);
  void* custom_realloc(void* ptr, size_t sz);

  // New hooks for allocation functions.
  static void* my_malloc_hook(size_t sz, const void* ptr);
  static void my_free_hook(void* ptr, const void*);
  static void* my_realloc_hook(void* ptr, size_t sz, const void*);

  // Store the old hooks just in case.
  static void* (*old_malloc_hook)(size_t sz, const void*);
  static void (*old_free_hook)(void* ptr, const void*);
  static void* (*old_realloc_hook)(void* ptr, size_t sz, const void*);

  static void* my_malloc_hook(size_t size, const void*) {
    return custom_malloc(size);
  }

  static void my_free_hook(void* ptr, const void*) {
    custom_free(ptr);
  }

  static void* my_realloc_hook(void* ptr, size_t sz, const void*) {
    return custom_realloc(ptr, sz);
  }

  static void my_init_hook(void) {
    // Store the old hooks.
    old_malloc_hook = __malloc_hook;
    old_free_hook = __free_hook;
    old_realloc_hook = __realloc_hook;
    // Point the hooks to the replacement functions.
    __malloc_hook = my_malloc_hook;
    __free_hook = my_free_hook;
    __realloc_hook = my_realloc_hook;
  }

  void (*__MALLOC_HOOK_VOLATILE __malloc_initialize_hook)(void) = my_init_hook;

}
#elif __APPLE__

#include <malloc/malloc.h>
#include <unistd.h>
#include <assert.h>


extern "C" {

  void* custom_malloc(size_t sz);
  void custom_free(void* ptr);

  // Takes a pointer and returns how much space it holds.
  size_t custom_malloc_usable_size(void* ptr);

  // Locks the heap(s), used prior to any invocation of fork().
  void custom_malloc_lock();

  // Unlocks the heap(s), after fork().
  void custom_malloc_unlock();

}


typedef struct interpose_s {
  void* new_func;
  void* orig_func;
} interpose_t;


#define MAC_INTERPOSE(newf, oldf) __attribute__((used)) \
  static const interpose_t macinterpose##newf##oldf \
  __attribute__((section("__DATA, __interpose"))) = \
    { reinterpret_cast<void*>(newf), reinterpret_cast<void*>(oldf) }


//////////
//////////

// All replacement functions get the prefix macwrapper_.

#define MACWRAPPER_PREFIX(n) macwrapper_##n

extern "C" {

  void* MACWRAPPER_PREFIX(malloc)(size_t sz) {
    // Mac OS ABI requires 16-byte alignment, so we round up the size
    // to the next multiple of 16.
    if (sz < 16) {
      sz = 16;
    }
    if (sz % 16 != 0) {
      sz += 16 - (sz % 16);
    }
    void* ptr = custom_malloc(sz);
    return ptr;
  }

  size_t MACWRAPPER_PREFIX(malloc_usable_size)(void* ptr) {
    if (ptr == NULL) {
      return 0;
    }
    size_t objSize = custom_malloc_usable_size(ptr);
    return objSize;
  }

  void MACWRAPPER_PREFIX(free)(void* ptr) {
    custom_free(ptr);
  }

  size_t MACWRAPPER_PREFIX(malloc_good_size)(size_t sz) {
    void* ptr = MACWRAPPER_PREFIX(malloc)(sz);
    size_t objSize = MACWRAPPER_PREFIX(malloc_usable_size)(ptr);
    MACWRAPPER_PREFIX(free)(ptr);
    return objSize;
  }

  static void* _extended_realloc(void* ptr, size_t sz, bool isReallocf) {
    if (ptr == NULL) {
      return MACWRAPPER_PREFIX(malloc)(sz);
    }

    // 0 size = free. We return a small object.  This behavior is
    // apparently required under Mac OS X and optional under POSIX.
    if (sz == 0) {
      MACWRAPPER_PREFIX(free)(ptr);
      return MACWRAPPER_PREFIX(malloc)(1);
    }

    size_t objSize = MACWRAPPER_PREFIX(malloc_usable_size)(ptr);

    // Custom logic here to ensure we only do a logarithmic number of
    // reallocations (with a constant space overhead).

    // Don't change size if the object is shrinking by less than half.
    if ((objSize / 2 < sz) && (sz <= objSize)) {
      // Do nothing.
      return ptr;
    }

    void* buf = MACWRAPPER_PREFIX(malloc)((size_t)(sz));

    if (buf != NULL) {
      // Successful malloc.
      // Copy the contents of the original object
      // up to the size of the new block.
      size_t minSize = (objSize < sz) ? objSize : sz;
      memcpy(buf, ptr, minSize);
      MACWRAPPER_PREFIX(free)(ptr);
    } else {
      if (isReallocf) {
        // Free the old block if the new allocation failed.
        // Specific behavior for Mac OS X reallocf().
        MACWRAPPER_PREFIX(free)(ptr);
      }
    }

    // Return a pointer to the new one.
    return buf;
  }

  void* MACWRAPPER_PREFIX(realloc)(void* ptr, size_t sz) {
    return _extended_realloc(ptr, sz, false);
  }

  void* MACWRAPPER_PREFIX(reallocf)(void* ptr, size_t sz) {
    return _extended_realloc(ptr, sz, true);
  }

  void* MACWRAPPER_PREFIX(calloc)(size_t elsize, size_t nelems) {
    size_t n = nelems * elsize;
    if (n == 0) {
      n = 1;
    }
    void* ptr = MACWRAPPER_PREFIX(malloc)(n);
    if (ptr) {
      memset(ptr, 0, n);
    }
    return ptr;
  }

  char* MACWRAPPER_PREFIX(strdup)(const char* s) {
    char* newString = NULL;
    if (s != NULL) {
      int len = strlen(s) + 1;
      if ((newString = reinterpret_cast<char*>(MACWRAPPER_PREFIX(malloc)(len)))) {
        memcpy(newString, s, len);
      }
    }
    return newString;
  }

  void* MACWRAPPER_PREFIX(memalign)(size_t alignment, size_t size) {
    // Check for non power-of-two alignment, or mistake in size.
    if ((alignment == 0) || (alignment & (alignment - 1))) {
      return NULL;
    }

    // Try to just allocate an object of the requested size.
    // If it happens to be aligned properly, just return it.
    void* ptr = MACWRAPPER_PREFIX(malloc)(size);
    if (((size_t) ptr & (alignment - 1)) == (size_t) ptr) {
      // It is already aligned just fine; return it.
      return ptr;
    }
    // It was not aligned as requested: free the object.
    MACWRAPPER_PREFIX(free)(ptr);
    // Now get a big chunk of memory and align the object within it.
    // NOTE: this assumes that the underlying allocator will be able
    // to free the aligned object, or ignore the free request.
    void* buf = MACWRAPPER_PREFIX(malloc)(2 * alignment + size);
    void* alignedPtr = reinterpret_cast<void*>((((size_t) buf + alignment - 1) & ~(alignment - 1)));
    return alignedPtr;
  }

  int MACWRAPPER_PREFIX(posix_memalign)(void** memptr, size_t alignment, size_t size) {
    // Check for non power-of-two alignment.
    if ((alignment == 0) || (alignment & (alignment - 1))) {
      return EINVAL;
    }

    void* ptr = MACWRAPPER_PREFIX(memalign)(alignment, size);
    if (!ptr) {
      return ENOMEM;
    } else {
      *memptr = ptr;
      return 0;
    }
  }

  void* MACWRAPPER_PREFIX(valloc)(size_t sz) {
    // Equivalent to memalign(pagesize, sz).
    void* ptr = MACWRAPPER_PREFIX(memalign)(PAGE_SIZE, sz);
    return ptr;
  }

}


extern "C" {

  // operator new
  void* _Znwm(int64_t);
  void* _Znam(int64_t);

  // operator delete
  void _ZdlPv(void* ptr);
  void _ZdaPv(void* ptr);

  // nothrow variants
  // operator new nothrow
  void* _ZnwmRKSt9nothrow_t();
  void* _ZnamRKSt9nothrow_t();
  // operator delete nothrow
  void _ZdaPvRKSt9nothrow_t(void* ptr);

  void _malloc_fork_prepare(void);
  void _malloc_fork_parent(void);
  void _malloc_fork_child(void);
}

static malloc_zone_t theDefaultZone;

extern "C" {

  unsigned MACWRAPPER_PREFIX(malloc_zone_batch_malloc)(malloc_zone_t*,
                   size_t sz,
                   void** results,
                   unsigned num_requested) {
    for (unsigned i = 0; i < num_requested; i++) {
      results[i] = MACWRAPPER_PREFIX(malloc)(sz);
      if (results[i] == NULL) {
  return i;
      }
    }
    return num_requested;
  }

  void MACWRAPPER_PREFIX(malloc_zone_batch_free)(malloc_zone_t*,
             void** to_be_freed,
             unsigned num) {
    for (unsigned i = 0; i < num; i++) {
      MACWRAPPER_PREFIX(free)(to_be_freed[i]);
    }
  }

  bool MACWRAPPER_PREFIX(malloc_zone_check)(malloc_zone_t*) {
    // Just return true for all zones.
    return true;
  }

  void MACWRAPPER_PREFIX(malloc_zone_print)(malloc_zone_t*, bool) {
    // Do nothing.
  }

  void MACWRAPPER_PREFIX(malloc_zone_log)(malloc_zone_t*, void*) {
    // Do nothing.
  }

  const char* MACWRAPPER_PREFIX(malloc_get_zone_name)(malloc_zone_t*) {
    return theDefaultZone.zone_name;
  }

  void MACWRAPPER_PREFIX(malloc_set_zone_name)(malloc_zone_t*, const char*) {
    // do nothing.
  }

  malloc_zone_t* MACWRAPPER_PREFIX(malloc_create_zone)(vm_size_t,
              unsigned) {
    return &theDefaultZone;
  }

  void MACWRAPPER_PREFIX(malloc_destroy_zone)(malloc_zone_t*) {
    // Do nothing.
  }

  malloc_zone_t* MACWRAPPER_PREFIX(malloc_zone_from_ptr)(const void*) {
    return NULL;
  }

  void* MACWRAPPER_PREFIX(malloc_default_zone)(void) {
    return reinterpret_cast<void*>(&theDefaultZone);
  }

  void MACWRAPPER_PREFIX(malloc_zone_free_definite_size)(malloc_zone_t*, void* ptr, size_t) {
    MACWRAPPER_PREFIX(free)(ptr);
  }

  void MACWRAPPER_PREFIX(malloc_zone_register)(malloc_zone_t*) {
  }

  void MACWRAPPER_PREFIX(malloc_zone_unregister)(malloc_zone_t*) {
  }

  int MACWRAPPER_PREFIX(malloc_jumpstart)(int) {
    return 1;
  }

  void* MACWRAPPER_PREFIX(malloc_zone_malloc)(malloc_zone_t*, size_t size) {
    return MACWRAPPER_PREFIX(malloc)(size);
  }

  void* MACWRAPPER_PREFIX(malloc_zone_calloc)(malloc_zone_t*, size_t n, size_t size) {
    return MACWRAPPER_PREFIX(calloc)(n, size);
  }

  void* MACWRAPPER_PREFIX(malloc_zone_valloc)(malloc_zone_t*, size_t size) {
    return MACWRAPPER_PREFIX(valloc)(size);
  }

  void* MACWRAPPER_PREFIX(malloc_zone_realloc)(malloc_zone_t*, void* ptr, size_t size) {
    return MACWRAPPER_PREFIX(realloc)(ptr, size);
  }

  void* MACWRAPPER_PREFIX(malloc_zone_memalign)(malloc_zone_t*, size_t alignment, size_t size) {
    return MACWRAPPER_PREFIX(memalign)(alignment, size);
  }

  void MACWRAPPER_PREFIX(malloc_zone_free)(malloc_zone_t*, void* ptr) {
    MACWRAPPER_PREFIX(free)(ptr);
  }

  size_t MACWRAPPER_PREFIX(internal_malloc_zone_size)(malloc_zone_t*, const void* ptr) {
    return MACWRAPPER_PREFIX(malloc_usable_size)(const_cast<void*>(ptr));
  }

  void MACWRAPPER_PREFIX(_malloc_fork_prepare)(void) {
    /* Prepare the malloc module for a fork by insuring that no thread is in a malloc critical section */
    custom_malloc_lock();
  }

  void MACWRAPPER_PREFIX(_malloc_fork_parent)(void) {
    /* Called in the parent process after a fork() to resume normal operation. */
    custom_malloc_unlock();
  }

  void MACWRAPPER_PREFIX(_malloc_fork_child)(void) {
    /*
     * Called in the child process after a fork() to resume normal operation.
     * In the MTASK case we also have to change memory inheritance so that the
     * child does not share memory with the parent.
     */
    custom_malloc_unlock();
  }

}

extern "C" void vfree(void* ptr);
extern "C" int malloc_jumpstart(int sz);

// Now interpose everything.

MAC_INTERPOSE(macwrapper_malloc, malloc);
MAC_INTERPOSE(macwrapper_valloc, valloc);
MAC_INTERPOSE(macwrapper_free, free);
MAC_INTERPOSE(macwrapper_realloc, realloc);
MAC_INTERPOSE(macwrapper_reallocf, reallocf);
MAC_INTERPOSE(macwrapper_calloc, calloc);
MAC_INTERPOSE(macwrapper_malloc_good_size, malloc_good_size);
MAC_INTERPOSE(macwrapper_strdup, strdup);
MAC_INTERPOSE(macwrapper_posix_memalign, posix_memalign);
MAC_INTERPOSE(macwrapper_malloc_default_zone, malloc_default_zone);

// Zone allocation calls.
MAC_INTERPOSE(macwrapper_malloc_zone_batch_malloc, malloc_zone_batch_malloc);
MAC_INTERPOSE(macwrapper_malloc_zone_batch_free, malloc_zone_batch_free);
MAC_INTERPOSE(macwrapper_malloc_zone_malloc, malloc_zone_malloc);
MAC_INTERPOSE(macwrapper_malloc_zone_calloc, malloc_zone_calloc);
MAC_INTERPOSE(macwrapper_malloc_zone_valloc, malloc_zone_valloc);
MAC_INTERPOSE(macwrapper_malloc_zone_realloc, malloc_zone_realloc);
MAC_INTERPOSE(macwrapper_malloc_zone_memalign, malloc_zone_memalign);
MAC_INTERPOSE(macwrapper_malloc_zone_free, malloc_zone_free);

// Zone access, etc.
MAC_INTERPOSE(macwrapper_malloc_get_zone_name, malloc_get_zone_name);
MAC_INTERPOSE(macwrapper_malloc_create_zone, malloc_create_zone);
MAC_INTERPOSE(macwrapper_malloc_destroy_zone, malloc_destroy_zone);
MAC_INTERPOSE(macwrapper_malloc_zone_check, malloc_zone_check);
MAC_INTERPOSE(macwrapper_malloc_zone_print, malloc_zone_print);
MAC_INTERPOSE(macwrapper_malloc_zone_log, malloc_zone_log);
MAC_INTERPOSE(macwrapper_malloc_set_zone_name, malloc_set_zone_name);
MAC_INTERPOSE(macwrapper_malloc_zone_from_ptr, malloc_zone_from_ptr);
MAC_INTERPOSE(macwrapper_malloc_zone_register, malloc_zone_register);
MAC_INTERPOSE(macwrapper_malloc_zone_unregister, malloc_zone_unregister);
MAC_INTERPOSE(macwrapper_malloc_jumpstart, malloc_jumpstart);

MAC_INTERPOSE(macwrapper__malloc_fork_prepare, _malloc_fork_prepare);
MAC_INTERPOSE(macwrapper__malloc_fork_parent, _malloc_fork_parent);
MAC_INTERPOSE(macwrapper__malloc_fork_child, _malloc_fork_child);
MAC_INTERPOSE(macwrapper_free, vfree);
MAC_INTERPOSE(macwrapper_malloc_usable_size, malloc_size);
MAC_INTERPOSE(macwrapper_malloc, _Znwm);
MAC_INTERPOSE(macwrapper_malloc, _Znam);

// ???
MAC_INTERPOSE(macwrapper_malloc, _ZnwmRKSt9nothrow_t);
MAC_INTERPOSE(macwrapper_malloc, _ZnamRKSt9nothrow_t);
MAC_INTERPOSE(macwrapper_free, _ZdlPv);
MAC_INTERPOSE(macwrapper_free, _ZdaPv);
MAC_INTERPOSE(macwrapper_free, _ZdaPvRKSt9nothrow_t);


/*
  not implemented, from libgmalloc:

__interpose_malloc_freezedry
__interpose_malloc_get_all_zones
__interpose_malloc_printf
__interpose_malloc_zone_print_ptr_info

*/

// A class to initialize exactly one malloc zone with the calls used
// by our replacement.

static const char* theOneTrueZoneName = "DefaultMallocZone";

class initializeDefaultZone {
 public:
    initializeDefaultZone() {
      theDefaultZone.size = MACWRAPPER_PREFIX(internal_malloc_zone_size);
      theDefaultZone.malloc = MACWRAPPER_PREFIX(malloc_zone_malloc);
      theDefaultZone.calloc = MACWRAPPER_PREFIX(malloc_zone_calloc);
      theDefaultZone.valloc = MACWRAPPER_PREFIX(malloc_zone_valloc);
      theDefaultZone.free = MACWRAPPER_PREFIX(malloc_zone_free);
      theDefaultZone.realloc = MACWRAPPER_PREFIX(malloc_zone_realloc);
      theDefaultZone.destroy = MACWRAPPER_PREFIX(malloc_destroy_zone);
      theDefaultZone.zone_name = theOneTrueZoneName;
      theDefaultZone.batch_malloc = MACWRAPPER_PREFIX(malloc_zone_batch_malloc);
      theDefaultZone.batch_free = MACWRAPPER_PREFIX(malloc_zone_batch_free);
      theDefaultZone.introspect = NULL;
      theDefaultZone.version = 1;
      theDefaultZone.memalign = MACWRAPPER_PREFIX(malloc_zone_memalign);
      theDefaultZone.free_definite_size = MACWRAPPER_PREFIX(malloc_zone_free_definite_size);
      theDefaultZone.pressure_relief = NULL;
      malloc_zone_register(&theDefaultZone);
      // Glorious hack to make OSX memory allocator override work from
      // http://google-perftools.googlecode.com/svn/tags/google-perftools-1.8/src/libc_override_osx.h
      malloc_zone_t* default_zone = malloc_default_zone();
      malloc_zone_unregister(default_zone);
      malloc_zone_register(default_zone);
    }
};

// Force initialization of the default zone.
static initializeDefaultZone initMe;

#else
#error "Unsupported platform!"
#endif
