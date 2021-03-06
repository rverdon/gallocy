#ifndef GALLOCY_HEAPLAYERS_PAGETABLEHEAP_H_
#define GALLOCY_HEAPLAYERS_PAGETABLEHEAP_H_


#include "allocators/internal.h"
#include "gallocy/models.h"
#include "gallocy/utils/logging.h"


namespace HL {

template <class Super>
class PageTableHeap : public Super {
 public:
  inline void *malloc(size_t sz) {
    void *ptr;
    ptr = Super::malloc(sz);
    LOG_DEBUG("Allocating " << sz << " byte(s) at " << ptr);
    return ptr;
  }

  inline void free(void *ptr) {
    Super::free(ptr);
  }

  inline void __reset() {
    Super::__reset();
  }
};

}  // namespace HL

#endif  // GALLOCY_HEAPLAYERS_PAGETABLEHEAP_H_
