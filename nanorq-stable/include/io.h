#ifndef NANORQ_IOCTX_H
#define NANORQ_IOCTX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _MSC_VER
#include "../include/mman-win32/trunk/mman.h"
#else
#endif

struct ioctx {
  size_t (*read)(struct ioctx *, uint8_t *, size_t);
  size_t (*write)(struct ioctx *, const uint8_t *, size_t);
  bool (*seek)(struct ioctx *, const size_t);
  size_t (*size)(struct ioctx *);
  long (*tell)(struct ioctx *);
  void (*destroy)(struct ioctx *);
  bool seekable;
  bool writable;
};

struct ioctx *ioctx_from_file(const char *fn, int t);
struct ioctx *ioctx_mmap_file(const char *fn, int t);
struct ioctx *ioctx_from_mem(const uint8_t *ptr, size_t t);

#endif
