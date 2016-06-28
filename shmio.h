#ifndef SHMIO_H
#define SHMIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include<sys/types.h>

size_t shmsize(void*shm);

void*shmfree(void*shm); 
void*shmalloc(size_t size);

int shmgive(int fd, void*shm);
void*shmtake(int fd, size_t*size);

void*shmcopy(void*shm);
void*shmview(void*shm);
int shmshare(int fd, void*shm);

int shmgivecopy(int fd, void*shm);
void* shmtakecopy(int fd, size_t*size);

int shmwrite(int fd, void*shm);
void*shmread(int fd, size_t*size);

#ifdef __cpluscplus
}
#endif

#endif

