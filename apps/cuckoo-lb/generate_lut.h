#ifndef __GEN_LUT_H__
#define __GEN_LUT_H__
#include "xxhash.h"
#include <cstdlib>
#include <string>
#include <vector>
inline static void compute_offset_and_skip(const char* name, size_t name_len,
                                           size_t lut_len, size_t& offset,
                                           size_t& skip) {
  size_t h0 = XXH64(name, name_len, 0);
  size_t h1 = XXH64(name, name_len, 1);
  offset = h1 % lut_len;
  skip = (h0 % (lut_len - 1)) + 1;
}

inline static size_t perm(size_t offset, size_t skip, size_t lut_len,
                          size_t j) {
  return (offset + skip * j) % lut_len;
}

inline static void generate_lut(const char* pfx, int lbs, size_t* lut,
                                size_t lut_len) {
  size_t *next = new size_t[lbs];
  for (size_t i = 0; i < lut_len; i++) {
    // Just a sentinel value for empty.
    lut[i] = 0x8000;
  }
  size_t n = 0;
  size_t *skip = new size_t[lbs];
  size_t *offset = new size_t[lbs];
  for (int i = 0; i < lbs; i++) {
    char name[64];
    snprintf(name, 64, "%s_%d", pfx, i);
    compute_offset_and_skip(name, 64, lut_len, offset[i], skip[i]);
    next[i] = 0;
  }
  while (n < lut_len) {
    for (int i = 0; i < lbs; i++) {
      size_t c = perm(offset[i], skip[i], lut_len, next[i]);
      size_t counter = 0;
      const size_t BREAK_COUNTER = 1000;
      while (lut[c] != 0x8000) {
        next[i]++;
        c = perm(offset[i], skip[i], lut_len, next[i]);
        counter++;
        if (counter > BREAK_COUNTER) {
          break;
        }
      }
      if (counter >= BREAK_COUNTER) {
        continue;
      }
      if (c >= lut_len) {
        abort();
      }
      lut[c] = i;
      next[i]++;
      n++;
      if (n >= lut_len) {
        break;
      }
    }
  }
  delete []next;
  delete []skip;
  delete []offset;
}
#endif
