#ifndef _SHM_SPIN_LOCK__H_
#define _SHM_SPIN_LOCK__H_

#include <stdint.h>

// fully occupying a single cache line
typedef uint64_t shm_spin_lock_t;

shm_spin_lock_t *shm_spin_lock_create(char *name);
shm_spin_lock_t *shm_spin_lock_attach(char *name);

shm_spin_lock_t *shm_spin_lock_array_create(char *name, uint32_t size);
shm_spin_lock_t *shm_spin_lock_array_attach(char *name, uint32_t size);

int shm_spin_lock(volatile shm_spin_lock_t *lock);
int shm_spin_trylock(volatile shm_spin_lock_t *lock);
int shm_spin_unlock(volatile shm_spin_lock_t *lock);

#endif
