#include "shm_spin_lock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <xmmintrin.h>

shm_spin_lock_t *shm_spin_lock_create(char *name) {
	int shm_fd = open(name, O_CREAT | O_RDWR | O_TRUNC, 0666);
	if (shm_fd < 0) {
		fprintf(stderr, "Fail to open %d\n", shm_fd);
		return NULL;
	}

	int ret = ftruncate(shm_fd, sizeof(shm_spin_lock_t));
	if (ret < 0) {
		fprintf(stderr, "Fail to truncate\n");
		return NULL;
	}

	shm_spin_lock_t *lock = (shm_spin_lock_t *) mmap(
			NULL, sizeof(shm_spin_lock_t), 
			PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (!lock){
		fprintf(stderr, "Fail to mmap\n");
		return NULL;
	}

	*lock = 0;
	return lock; 
}

shm_spin_lock_t *shm_spin_lock_attach(char *name) {
	int shm_fd = open(name, O_RDWR);
	if (shm_fd < 0) {
		fprintf(stderr, "Fail to open\n");
		return NULL;
	}

	shm_spin_lock_t *lock = (shm_spin_lock_t *) mmap(
			NULL, sizeof(shm_spin_lock_t),
			PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (!lock){
		fprintf(stderr, "Fail to mmap\n");
		return NULL;
	}

	return lock;
}

shm_spin_lock_t *shm_spin_lock_array_create(char *name, uint32_t size) {
	int shm_fd = open(name, O_CREAT | O_RDWR | O_TRUNC, 0666);
	if (shm_fd < 0) {
		fprintf(stderr, "Fail to open %d\n", shm_fd);
		return NULL;
	}

	int ret = ftruncate(shm_fd, sizeof(shm_spin_lock_t) * size);
	if (ret < 0) {
		fprintf(stderr, "Fail to truncate\n");
		return NULL;
	}

	shm_spin_lock_t *lock = (shm_spin_lock_t *) mmap(
			NULL, sizeof(shm_spin_lock_t) * size,
			PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (!lock){
		fprintf(stderr, "Fail to mmap\n");
		return NULL;
	}

	memset(lock, 0, sizeof(shm_spin_lock_t) * size);
	return lock;
}

shm_spin_lock_t *shm_spin_lock_array_attach(char *name, uint32_t size) {
	int shm_fd = open(name, O_RDWR);
	if (shm_fd < 0) {
		fprintf(stderr, "Fail to open\n");
		return NULL;
	}

	shm_spin_lock_t *lock = (shm_spin_lock_t *) mmap(
			NULL, sizeof(shm_spin_lock_t) * size,
			PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (!lock){
		fprintf(stderr, "Fail to mmap\n");
		return NULL;
	}

	return lock;
}

int shm_spin_lock(volatile shm_spin_lock_t *lock) {
    while (__sync_lock_test_and_set(lock, 1))
		while (*lock == 1) {
			_mm_pause();
		}
	return 0;
}

int shm_spin_trylock(volatile shm_spin_lock_t *lock) {
	switch (__sync_lock_test_and_set(lock, 1)) {
		case 0: 
			return 0;
		case 1:
			return EBUSY;
	}
	return EINVAL;
}

int shm_spin_unlock(volatile shm_spin_lock_t *lock) {
	__sync_lock_release(lock);
	return 0;
}
