// Copyright (c) 2014-2018, The Regents of the University of California.
// Derived from Bess source code which is
// Copyright (c) 2016-2017, Nefeli Networks Inc.

#ifndef __CMAP_H__
#define __CMAP_H__

#ifdef TASVIR
#include "tasvir.h"
#else
#include "shm_spin_lock.h"
#endif

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <limits>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <utility>
#include <xmmintrin.h>
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef __unused
#define __unused __attribute__((__unused__))
#endif

//#define BIG_LOCK 1

typedef uint32_t HashResult;
typedef uint32_t EntryIndex;

template <typename K, typename V>
struct TrivialPair {
  K first;
  V second;
};

struct NoopMarker {
  void operator()(__unused const void* address, __unused size_t size) const {
    // NOP
  }
};

#ifdef TASVIR
// Use this when using Tasvir, it marks things.
struct TasvirMarker {
  void operator()(const void* address, size_t size) const {
    tasvir_log_write(address, size);
  }
};
#endif

// A simple stack which can be allocated on some memory.
template <typename M = NoopMarker>
class EntryStack {
 public:
  static size_t BytesForEntries(size_t entries) {
    return sizeof(EntryStack) + entries * sizeof(EntryIndex);
  }

  static EntryStack* Create(void* area, size_t area_size, size_t entries) {
    if (area_size < BytesForEntries(entries)) {
      return nullptr;
    }

    // Layout
    // EntryStack
    // stck[0]
    // ...
    // stck[entries]
    EntryStack* ret = (EntryStack*)area;
    ret->head_ = 0;
    ret->size_ = entries;
    ret->stck_ = (EntryIndex*)(ret + 1);
    M()(ret, sizeof(EntryStack<M>));
    return ret;
  }

  int Push(EntryIndex entry) {
    if (head_ >= size_) {
      return 0;
    }

    head_++;
    stck_[head_] = entry;
    M()(&stck_[head_], sizeof(size_t));
    M()(&head_, sizeof(size_t));
    return 1;
  }

  EntryIndex Top() { return stck_[head_]; }

  int Pop() {
    if (head_ == 0) {
      return 0;
    }
    head_--;
    M()(&head_, sizeof(size_t));
    return 1;
  }

  void Clear() {
    head_ = 0;
    M()(&head_, sizeof(size_t));
  }

  inline bool Empty() { return head_ == 0; }

 private:
  size_t head_;
  size_t size_;
  EntryIndex* stck_;
};

// A Hash table implementation using cuckoo hashing
//
// Example usage:
//
//  CuckooMap<uint32_t, uint64_t> cuckoo;
//  cuckoo.Insert(1, 99);
//  std::pair<uint32_t, uint64_t>* result = cuckoo.Find(1)
//  std::cout << "key: " << result->first << ", value: "
//    << result->second << std::endl;
//
// The output should be "key: 1, value: 99"
//
// For more examples, please refer to cuckoo_map_test.cc

template <typename K, typename V, typename H = std::hash<K>,
          typename E = std::equal_to<K>, typename M = NoopMarker>
class CuckooMap {
 public:
  typedef TrivialPair<K, V> Entry;
  static_assert(std::is_trivially_copyable<Entry>(),
                "Entry should be trivially copyable");
  class iterator {
   public:
    using difference_type = std::ptrdiff_t;
    using value_type = Entry;
    using pointer = Entry*;
    using reference = Entry&;
    using iterator_category = std::forward_iterator_tag;

    iterator(CuckooMap& map, size_t bucket, size_t slot)
        : map_(map), bucket_idx_(bucket), slot_idx_(slot) {
      while (bucket_idx_ < map_.alloc_num_buckets_ &&
             map_.buckets_[bucket_idx_].hash_values[slot_idx_] == 0) {
        slot_idx_++;
        if (slot_idx_ == kEntriesPerBucket) {
          slot_idx_ = 0;
          bucket_idx_++;
        }
      }
    }

    iterator& operator++() {  // Pre-increment
      do {
        slot_idx_++;
        if (slot_idx_ == kEntriesPerBucket) {
          slot_idx_ = 0;
          bucket_idx_++;
        }
      } while (bucket_idx_ < map_.alloc_num_buckets_ &&
               map_.buckets_[bucket_idx_].hash_values[slot_idx_] == 0);
      return *this;
    }

    iterator operator++(int) {  // Pre-increment
      iterator tmp(*this);
      do {
        slot_idx_++;
        if (slot_idx_ == kEntriesPerBucket) {
          slot_idx_ = 0;
          bucket_idx_++;
        }
      } while (bucket_idx_ < map_.alloc_num_buckets_ &&
               map_.buckets_[bucket_idx_].hash_values[slot_idx_] == 0);
      return tmp;
    }

    bool operator==(const iterator& rhs) const {
      return &map_ == &rhs.map_ && bucket_idx_ == rhs.bucket_idx_ &&
             slot_idx_ == rhs.slot_idx_;
    }

    bool operator!=(const iterator& rhs) const {
      return &map_ != &rhs.map_ || bucket_idx_ != rhs.bucket_idx_ ||
             slot_idx_ != rhs.slot_idx_;
    }

    reference operator*() {
      EntryIndex idx = map_.buckets_[bucket_idx_].entry_indices[slot_idx_];
      return map_.entries_[idx];
    }

    pointer operator->() {
      EntryIndex idx = map_.buckets_[bucket_idx_].entry_indices[slot_idx_];
      return &map_.entries_[idx];
    }

   private:
    CuckooMap& map_;
    size_t bucket_idx_;
    size_t slot_idx_;
  };

  // Memory layout
  // CuckooMap struct
  // EntryStack
  // Entries
  // Buckets
  static size_t Size(size_t entries, size_t buckets) {
    return sizeof(CuckooMap) + EntryStack<M>::BytesForEntries(entries) +
           sizeof(Entry) * entries + sizeof(Bucket) * buckets;
  }

  // We require buckets to be a power of 2.
  static CuckooMap* Create(void* area, size_t area_size, size_t entries,
                           size_t buckets) {
    size_t total_size = Size(entries, buckets);
    if (area_size < total_size) {
      return nullptr;
    }
    if (buckets == 0 || entries == 0 || buckets & (buckets - 1) ||
        entries < buckets) {
      return nullptr;
    }
    CuckooMap* ret = (CuckooMap*)area;
    void* estack = (void*)(ret + 1);
    ret->free_entry_indices_ =
        EntryStack<M>::Create(estack, area_size - sizeof(CuckooMap), entries);
    if (ret->free_entry_indices_ == nullptr) {
      return nullptr;
    }
    void* entry_array =
        (void*)((uint8_t*)estack + EntryStack<M>::BytesForEntries(entries));
    ret->entries_ = (Entry*)(entry_array);
    ret->buckets_ = (Bucket*)(ret->entries_ + entries);
    ret->alloc_num_buckets_ = buckets;
    ret->alloc_num_entries_ = entries;
    ret->num_entries_ = 0;
    ret->bucket_mask_ = buckets - 1;
    ret->Clear();
    return ret;
  }

  // Not allowing copying for now
  CuckooMap(CuckooMap&) = delete;
  CuckooMap& operator=(CuckooMap&) = delete;
  // No default constructor
  CuckooMap() = delete;

  // Allow move
  CuckooMap(CuckooMap&&) = delete;
  CuckooMap& operator=(CuckooMap&&) = delete;

  iterator begin() { return iterator(*this, 0, 0); }
  iterator end() { return iterator(*this, alloc_num_buckets_, 0); }

  // Insert/update a key value pair
  // Return the pointer to the inserted entry
  Entry* Insert(const K& key, const V& value, const H& hasher = H(),
                const E& eq = E()) {
    Entry* entry = NULL;
    HashResult primary = Hash(key, hasher);

    EntryIndex idx = FindWithHash(primary, key, eq);
    if (idx != kInvalidEntryIdx) {
      entry = &entries_[idx];
      entry->second = value;
      M()(&entries_[idx], sizeof(Entry));
      return entry;
    }

    HashResult secondary = HashSecondary(primary);

    entry = AddEntry(primary, secondary, key, value, hasher);
    return entry;
  }

  // Find the pointer to the stored value by the key.
  // Return nullptr if not exist.
  Entry* Find(const K& key, const H& hasher = H(), const E& eq = E()) {
    EntryIndex idx = FindWithHash(Hash(key, hasher), key, eq);
    if (idx == kInvalidEntryIdx) {
      return nullptr;
    }

    Entry* ret = &entries_[idx];
    return ret;
  }

  // const version of Find()
  const Entry* Find(const K& key, const H& hasher = H(),
                    const E& eq = E()) const {
    EntryIndex idx = FindWithHash(Hash(key, hasher), key, eq);
    if (idx == kInvalidEntryIdx) {
      return nullptr;
    }

    const Entry* ret = &entries_[idx];
    if (ret == nullptr) {
      __builtin_unreachable();
    }
    return ret;
  }

  // Remove the stored entry by the key
  // Return false if not exist.
  bool Remove(const K& key, const H& hasher = H(), const E& eq = E()) {
    HashResult pri = Hash(key, hasher);
    if (RemoveFromBucket(pri, pri & bucket_mask_, key, eq)) {
      return true;
    }
    HashResult sec = HashSecondary(pri);
    if (RemoveFromBucket(pri, sec & bucket_mask_, key, eq)) {
      return true;
    }
    return false;
  }

#ifndef TASVIR
  Entry* InsertWithLock(shm_spin_lock_t *lock, const K& key, const V& value,
		  const H& hasher = H(), const E& eq = E()) {
	  SpinLock(lock, key, hasher);
	  Entry *entry = Insert(key, value, hasher, eq);
	  SpinUnlock(lock, key, hasher);
	  return entry;
  }

  Entry* FindWithLock(shm_spin_lock_t *lock, const K& key,
		  const H& hasher = H(), const E& eq = E()) {
	  SpinLock(lock, key, hasher);
	  Entry *entry = Find(key, hasher, eq);
	  SpinUnlock(lock, key, hasher);
	  return entry;
  }

  const Entry* FindWithLock(shm_spin_lock_t *lock, const K& key,
		  const H& hasher = H(), const E& eq = E()) const {
	  SpinLockLazy(lock, key, hasher);
	  const Entry *entry = Find(key, hasher, eq);
	  SpinUnlock(lock, key, hasher);
	  return entry;
  }

  bool RemoveWithLock(shm_spin_lock_t *lock, const K& key,
		  const H& hasher = H(), const E& eq = E()) {
	  SpinLockLazy(lock, key, hasher);
	  bool ret = Remove(key, hasher, eq);
	  SpinUnlock(lock, key, hasher);
	  return ret;
  }
#endif // #ifndef TASVIR

  void Clear() {
    free_entry_indices_->Clear();
    memset(buckets_, 0, sizeof(Bucket) * alloc_num_buckets_);
    memset(entries_, 0, sizeof(Entry) * alloc_num_entries_);
    num_entries_ = 0;
    bucket_mask_ = alloc_num_buckets_ - 1;
    for (int i = alloc_num_entries_ - 1; i >= 0; --i) {
      free_entry_indices_->Push(i);
    }
    // Need to invalidate everything since we reinitialized stuff.
    M()(this, Size(alloc_num_entries_, alloc_num_buckets_));
  }

  // Return the number of stored entries
  size_t Count() const { return num_entries_; }

 protected:
  // Tunable macros
  static const int kEntriesPerBucket = 4;  // 4-way set associative

  // 4^kMaxCuckooPath buckets will be considered to make a empty slot,
  // before giving up and expand the table.
  // Higher number will yield better occupancy, but the worst case performance
  // of insertion will grow exponentially, so be careful.
  static const int kMaxCuckooPath = 3;

  /* non-tunable macros */
  static const EntryIndex kInvalidEntryIdx =
      std::numeric_limits<EntryIndex>::max();

  struct Bucket {
    HashResult hash_values[kEntriesPerBucket];
    EntryIndex entry_indices[kEntriesPerBucket];

    Bucket() : hash_values(), entry_indices() {}
  };

  // Push an unused entry index back to the  stack
  void PushFreeEntryIndex(EntryIndex idx) { free_entry_indices_->Push(idx); }

#ifndef TASVIR
  void SpinLock(shm_spin_lock_t *lock, const K& key, const H& hasher) const {
#if BIG_LOCK
	shm_spin_lock(lock);
#else
    HashResult primary = Hash(key, hasher);
    HashResult secondary = HashSecondary(primary);

    int a = primary & bucket_mask_;
    int b = secondary & bucket_mask_;

    if (a < b) {
	  shm_spin_lock(&lock[a]);
	  shm_spin_lock(&lock[b]);
	} else if (a > b) {
	  shm_spin_lock(&lock[b]);
	  shm_spin_lock(&lock[a]);
	} else {
	  shm_spin_lock(&lock[a]);
    }
#endif // #if BIG_LOCK
  }

  void SpinLockLazy(shm_spin_lock_t *lock, const K& key, const H& hasher) const {
#if BIG_LOCK
	shm_spin_lock(lock);
#else
    HashResult primary = Hash(key, hasher);
    HashResult secondary = HashSecondary(primary);

    int a = primary & bucket_mask_;
    int b = secondary & bucket_mask_;

	if (a < b) {
		while(1) {
			if (shm_spin_trylock(&lock[a])) {
				if (shm_spin_trylock(&lock[b]))
					break;
				else
					shm_spin_unlock(&lock[a]);
			}
			_mm_pause();
		}
	} else if (a > b) {
		while (1) {
			if (shm_spin_trylock(&lock[b])) {
				if (shm_spin_trylock(&lock[a]))
					break;
				else
					shm_spin_unlock(&lock[b]);
			}
			_mm_pause();
		}
	} else {
	  shm_spin_lock(&lock[a]);
    }
#endif // #if BIG_LOCK
  }


  void SpinUnlock(shm_spin_lock_t *lock, const K& key, const H& hasher) const {
#if BIG_LOCK
	shm_spin_unlock(lock);
#else
    HashResult primary = Hash(key, hasher);
    HashResult secondary = HashSecondary(primary);

    int a = primary & bucket_mask_;
    int b = secondary & bucket_mask_;

    if (a < b) {
	  shm_spin_unlock(&lock[b]);
	  shm_spin_unlock(&lock[a]);
    } else if (a > b) {
	  shm_spin_unlock(&lock[a]);
	  shm_spin_unlock(&lock[b]);
    } else {
	  shm_spin_unlock(&lock[a]);
    }
#endif // #if BIG_LOCK
  }
#endif // #ifndef TASVIR

  // Pop a free entry index from stack and return the index
  EntryIndex PopFreeEntryIndex() {
    if (free_entry_indices_->Empty()) {
      return kInvalidEntryIdx;
    }
    size_t idx = free_entry_indices_->Top();
    free_entry_indices_->Pop();
    return idx;
  }

  // Try to add (key, value) to the bucket indexed by bucket_idx
  // Return the pointer to the entry if success. Otherwise return nullptr.
  Entry* AddToBucket(HashResult bucket_idx, const K& key, const V& value,
                     const H& hasher) {
    Bucket* bucket = &buckets_[bucket_idx];
    int slot_idx = FindEmptySlot(bucket);
    if (slot_idx == -1) {
      return nullptr;
    }

    EntryIndex free_idx = PopFreeEntryIndex();

    if (free_idx == kInvalidEntryIdx) {
      return nullptr;
    }

    bucket->hash_values[slot_idx] = Hash(key, hasher);
    bucket->entry_indices[slot_idx] = free_idx;
    M()(bucket, sizeof(Bucket));

    Entry* entry = &entries_[free_idx];
    entry->first = key;
    entry->second = value;
    M()(entry, sizeof(Entry));

    num_entries_++;
    return entry;
  }

  // Remove key from the bucket indexed by bucket_idx
  // Return true if success.
  bool RemoveFromBucket(HashResult primary, HashResult bucket_idx, const K& key,
                        const E& eq) {
    Bucket* bucket = &buckets_[bucket_idx];

    int slot_idx = FindSlot(bucket, primary, key, eq);
    if (slot_idx == -1) {
      return false;
    }

    bucket->hash_values[slot_idx] = 0;
    M()(buckets_, sizeof(Bucket));

    EntryIndex idx = bucket->entry_indices[slot_idx];
    entries_[idx] = Entry();
    M()(&entries_[idx], sizeof(Entry));
    M()(&num_entries_, sizeof(size_t));
    PushFreeEntryIndex(idx);

    num_entries_--;
    return true;
  }

  // Find key from the bucket indexed by bucket_idx
  // Return the index of the entry if success. Otherwise return nullptr.
  EntryIndex GetFromBucket(HashResult primary, HashResult bucket_idx,
                           const K& key, const E& eq) const {
    const Bucket* bucket = &buckets_[bucket_idx];

    int slot_idx = FindSlot(bucket, primary, key, eq);
    if (slot_idx == -1) {
      return kInvalidEntryIdx;
    }

    EntryIndex idx = bucket->entry_indices[slot_idx];
    if (idx == kInvalidEntryIdx) {
      __builtin_unreachable();
    }
    return idx;
  }

  // Try to add the entry (key, value)
  // Return the pointer to the entry if success. Otherwise return nullptr.
  Entry* AddEntry(HashResult primary, HashResult secondary, const K& key,
                  const V& value, const H& hasher) {
    HashResult primary_bucket_index, secondary_bucket_index;
    Entry* entry = nullptr;
  again:
    primary_bucket_index = primary & bucket_mask_;
    if ((entry = AddToBucket(primary_bucket_index, key, value, hasher)) !=
        nullptr) {
      return entry;
    }

    secondary_bucket_index = secondary & bucket_mask_;
    if ((entry = AddToBucket(secondary_bucket_index, key, value, hasher)) !=
        nullptr) {
      return entry;
    }

    if (MakeSpace(primary_bucket_index, 0, hasher) >= 0) {
      goto again;
    }

    if (MakeSpace(secondary_bucket_index, 0, hasher) >= 0) {
      goto again;
    }

    return nullptr;
  }

  // Return an empty slot index in the bucket
  int FindEmptySlot(const Bucket* bucket) const {
    for (int i = 0; i < kEntriesPerBucket; i++) {
      if (bucket->hash_values[i] == 0) {
        return i;
      }
    }
    return -1;
  }

  // Return the slot index in the bucket that matches the primary hash_value
  // and the actual key. Return -1 if not found.
  int FindSlot(const Bucket* bucket, HashResult primary, const K& key,
               const E& eq) const {
    for (int i = 0; i < kEntriesPerBucket; i++) {
      if (bucket->hash_values[i] == primary) {
        EntryIndex idx = bucket->entry_indices[i];
        const Entry* entry = &entries_[idx];

        if (likely(Eq(entry->first, key, eq))) {
          return i;
        }
      }
    }
    return -1;
  }

  // Recursively try making an empty slot in the bucket
  // Returns a slot index in [0, kEntriesPerBucket) for successful operation,
  // or -1 if failed.
  int MakeSpace(HashResult index, int depth, const H& hasher) {
    if (depth >= kMaxCuckooPath) {
      return -1;
    }

    Bucket* bucket = &buckets_[index];

    for (int i = 0; i < kEntriesPerBucket; i++) {
      EntryIndex idx = bucket->entry_indices[i];
      const K& key = entries_[idx].first;
      HashResult pri = Hash(key, hasher);
      HashResult sec = HashSecondary(pri);

      HashResult alt_index;

      // this entry is in its primary bucket?
      if (pri == bucket->hash_values[i]) {
        alt_index = sec & bucket_mask_;
      } else if (sec == bucket->hash_values[i]) {
        alt_index = pri & bucket_mask_;
      } else {
        return -1;
      }

      int j = FindEmptySlot(&buckets_[alt_index]);
      if (j == -1) {
        j = MakeSpace(alt_index, depth + 1, hasher);
      }
      if (j >= 0) {
        Bucket* alt_bucket = &buckets_[alt_index];
        alt_bucket->hash_values[j] = bucket->hash_values[i];
        alt_bucket->entry_indices[j] = bucket->entry_indices[i];
        bucket->hash_values[i] = 0;
        M()(&buckets_[alt_index], sizeof(Bucket));
        M()(&buckets_[index], sizeof(Bucket));
        return i;
      }
    }

    return -1;
  }

  // Get the entry given the primary hash value of the key.
  // Returns the pointer to the entry or nullptr if failed.
  EntryIndex FindWithHash(HashResult primary, const K& key, const E& eq) const {
    EntryIndex ret = GetFromBucket(primary, primary & bucket_mask_, key, eq);
    if (ret != kInvalidEntryIdx) {
      return ret;
    }
    return GetFromBucket(primary, HashSecondary(primary) & bucket_mask_, key,
                         eq);
  }

  // Secondary hash value
  static HashResult HashSecondary(HashResult primary) {
    HashResult tag = primary >> 12;
    return primary ^ ((tag + 1) * 0x5bd1e995);
  }

  // Primary hash value. Should always be non-zero (= not empty)
  static HashResult Hash(const K& key, const H& hasher) {
    return hasher(key) | (1u << 31);
  }

  static bool Eq(const K& lhs, const K& rhs, const E& eq) {
    return eq(lhs, rhs);
  }

  // # of buckets == mask + 1
  HashResult bucket_mask_;

  // # of entries
  size_t num_entries_;

  int alloc_num_buckets_;
  int alloc_num_entries_;

  // Stack of free entries
  EntryStack<M>* free_entry_indices_;

  // bucket and entry arrays grow independently
  Bucket* buckets_;
  Entry* entries_;
};

#endif  // __CMAP_H__
