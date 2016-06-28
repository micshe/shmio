#include<sys/mman.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<fcntl.h> 
#include<poll.h>

#include<unistd.h>

#include<errno.h>
#include<string.h> 
#include<limits.h>

#include<stdio.h>

#define SHM_PGSZ sysconf(_SC_PAGESIZE)

static int shm_waitread(int fd, int timelimit)
{
	/**
	pause the process until
	@fd is ready to be read,
	or longer than @timelimit
	milliseconds has passed.
	**/

	struct pollfd fds;
	fds.fd=fd;
	fds.events=POLLIN;
	int err;
	err = poll(&fds,1,timelimit);
	if(err==-1 || (fds.revents&POLLIN)!=POLLIN)
		return -errno;

	return ((errno=0),0);
}
static int shm_waitwrite(int fd, int timelimit)
{
	/**
	pause the process until
	@fd is ready to be written
	to, or longer than @timelimit
	milliseconds has passed.
	**/

	struct pollfd fds;
	fds.fd=fd;
	fds.events=POLLOUT;
	int err;
	err = poll(&fds,1,timelimit);
	if(err==-1 || (fds.revents&POLLOUT)!=POLLOUT)
		return -errno;

	return ((errno=0),0);
}
static size_t shm_readall(int fd, unsigned char*buf, size_t len)
{
	/**
	perform a blocking read,
	even if the fd is set to
	nonblocking.
	**/

	size_t e;
	if(len>SSIZE_MAX)
	{
		e = shm_readall(fd,buf,SSIZE_MAX);
		if(e<SSIZE_MAX)
			return e;

		buf = buf + SSIZE_MAX;
		len = len - SSIZE_MAX;
	}

	ssize_t err;
	err = ((errno=0),recv(fd,buf,len,MSG_WAITALL|MSG_NOSIGNAL));
	if(err>=0)
		return err; 
	if(err<0 && errno != ENOTSOCK)
		return 0;

	size_t i;
	for(i=0;i<len;)
	{
		shm_waitread(fd,-1);
		err = ((errno=0),read(fd,buf+i,len-i));
		if(err<0)
		{
			if(errno==EWOULDBLOCK || errno==EAGAIN || errno==EINTR)
				continue;
			else
				return i;
		} 
		i+=err;
	} 
	return ((errno=0),len); 
}
static size_t shm_writeall(int fd, unsigned char*buf, size_t len)
{
	/**
	perform a blockign write,
	even if the fd is set to
	nonblocking.
	**/

	size_t e;
	if(len>SSIZE_MAX)
	{
		e = shm_writeall(fd,buf,SSIZE_MAX);
		if(e<SSIZE_MAX)
			return e;

		buf = buf + SSIZE_MAX;
		len = len - SSIZE_MAX;
	}

	ssize_t err;
	err = ((errno=0),send(fd,buf,len,MSG_WAITALL|MSG_NOSIGNAL));
	if(err>=0)
		return err;
	if(err<0 && errno != ENOTSOCK)
		return 0;
	size_t i;
	for(i=0;i<len;)
	{
		shm_waitwrite(fd,-1);
		err = ((errno=0),write(fd,buf+i,len-i));
		if(err<0)
		{
			if(errno==EWOULDBLOCK || errno==EAGAIN || errno==EINTR)
				continue;
			else
				return i;
		} 
		i+=err;
	} 
	return ((errno=0),len);
}
static size_t shm_readlength(int fd)
{
	/**
	consume *all* data from fd until
	we hit the regex pattern /[1-9][0-9]*:/
	then we stop and return the long,
	unsigned integer that we read.

	returns a size of 0 on failure.
	**/
	int err;
	size_t running;
	unsigned char c;

	for(running=0;;)
	{
		err = shm_readall(fd,&c,1);
		if(err==0)
			return 0;

		if((running==0 && c>='1' && c<='9') ||
		   (running>0 && c>='0' && c<='9'))
			running = running * 10 + (c-'0');
		else if(running>0 && c==':')
			break;
		else
			running = 0;
	}

	return ((errno=0),running);
} 
static int shm_setflags(int fd,int flags)
{
	/* FIXME handle errors */

	if((flags&O_CLOEXEC)==O_CLOEXEC)
		fcntl(fd,F_SETFD,FD_CLOEXEC);

	return fcntl(fd,F_SETFL,flags); 
} 
static int shm_give(int fd, int payload)
{
	/**
	give an fd to another process
	over a duplex socket.  the fd
	no longer exists in the giving
	process.
	**/
	char byte;
        byte = '\0';
        struct iovec unused;
        unused.iov_base = &byte;
        unused.iov_len = sizeof(char);

        struct body
        {
                /* these have to be stored end-to-end */
                struct cmsghdr body;
                int payloadfd;
        };

        struct msghdr head;
        struct body body;
        memset(&body,0,sizeof(struct body));

        head.msg_name = NULL;
        head.msg_namelen = 0;
        head.msg_flags = 0;
        head.msg_iov = &unused;
        head.msg_iovlen = 1;

        head.msg_control = &body;
        head.msg_controllen = sizeof(body);

        struct cmsghdr *interface;
        interface = CMSG_FIRSTHDR(&head); 
        interface->cmsg_level = SOL_SOCKET;
        interface->cmsg_type = SCM_RIGHTS; 
        interface->cmsg_len = CMSG_LEN(1*sizeof(int)); 
        *((int*)CMSG_DATA(interface)) = payload;

	int err;
	err = sendmsg(fd, &head, MSG_WAITALL);
	if(err<0)
		return -errno;

	/* 
	it's less confusing if the fd only
	exists in one process at a time, so
	we close if the send succeeded.
	*/
	close(payload);

	return ((errno=0),0);	
}
static int shm_take(int fd, int flags)
{
	/**
	take an fd that was given by
	another process over a duplex
	socket.
	**/
	int err;

	/* we recv an fd over the socket @fd */ 
	char byte;
	byte = '\0';
	struct iovec unused;
	unused.iov_base = &byte;
	unused.iov_len = sizeof(char);

	struct body
	{
		/* these have to be stored end-to-end */
		struct cmsghdr body;
		int fd;
	}; 
	struct msghdr head;
	struct body body;
	memset(&body,0,sizeof(struct body));

	head.msg_name = NULL;
	head.msg_namelen = 0;
	head.msg_flags = 0;
	head.msg_iov = &unused;
	head.msg_iovlen = 1; 
	head.msg_control = &body;
	head.msg_controllen = sizeof(body);

	struct cmsghdr *interface;
	interface = CMSG_FIRSTHDR(&head); 
	interface->cmsg_level = SOL_SOCKET;
	interface->cmsg_type = SCM_RIGHTS; 
	interface->cmsg_len = CMSG_LEN(1*sizeof(int));

	int f=0;
#ifdef MSG_CMSG_CLOEXEC
	/*
	if this is available, then take() is
	threadsafe, and will not leak under
	thread-exec or thread-fork-exec events.
	*/
	f=MSG_CMSG_CLOEXEC;
#endif
	err = recvmsg(fd, &head, MSG_WAITALL|f);
	if(err<0)
		return -errno;

	if(interface->cmsg_len != CMSG_LEN(1*sizeof(int)))
		/* 
		we've not go the wrong message somehow

		FIXME check and see if we have 
		actually received them?  do we 
		need to close them?  how do we 
		find out how many we recieved? 
		*/
		return -errno;

	/* return the new fd that we recieved in the payload */
	fd = *((int*)CMSG_DATA(interface)); 
	err = shm_setflags(fd,flags);
	if(err==-1)
	{
		err = errno;
			close(fd);
		errno = err;
		return -errno;
	}

	return ((errno=0),fd);
}

static size_t shm_fdsize(int fd)
{
	/**
	return the size of the 
	regular file behind an
	fd.
	**/
	int err;

	struct stat meta;
	err = fstat(fd,&meta);
	if(err<0)
		return 0;

	if(!S_ISREG(meta.st_mode))
		/* this is what mmap uses for a NOT-A-FILE error */
		return ((errno=EACCES),0);

	return ((errno=0),meta.st_size);
} 
static int shm_shmfd(void*shm)
{
	/**
	return the fd behind
	a shm buffer.
	**/
	unsigned char*ptr;
	ptr = shm;

	int *fd;
	fd = (int*)(ptr-SHM_PGSZ);
	return ((errno=0),*fd);
}
size_t shmsize(void*shm)
{
	/**
	return the size in bytes
	of an shm buffer, or 0
	on failure.
	**/
	return ((errno=0),shm_fdsize(shm_shmfd(shm)));
} 

void*shmattach(int fd, size_t*size)
{
	/**
	unpackes a file-descriptor into
	a shm, and returns the shm pointer,
	and buffer size in bytes.
	**/ 
	size_t length;
	length = shm_fdsize(fd);
	if(length==0)
		return NULL;

	unsigned char*head;
	head = mmap(NULL, SHM_PGSZ+length, PROT_READ|PROT_WRITE, 
	            MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if(head==(void*)-1)
		return NULL;

	/* FIXME should we dup() and close the @fd? */
	*((int*)head) = fd;

	/* for convience only.  offers no real safety. */
	int err;
	err = ((errno=0),mprotect(head, SHM_PGSZ, PROT_READ));
	if(err<0)
		goto fail;

	unsigned char*body;
	body = mmap(head + SHM_PGSZ, length, PROT_READ|PROT_WRITE,
	            MAP_FIXED|MAP_SHARED,fd,0);
	if(body==(void*)-1)
		goto fail;

	if(size!=NULL) 
		*size = length;

	return ((errno=0),body);

fail:
	err = errno;
		munmap(head,length+SHM_PGSZ);
	errno = err;
	return NULL; 
}
int shmdetach(void*shm)
{
	/**
	packs up a shm and returns
	the underlying file descriptor.
	**/
	int fd;
	fd = shm_shmfd(shm);
	if(fd<0)
		return -errno;

	int err;
	err = munmap(shm-SHM_PGSZ, SHM_PGSZ + shmsize(shm));
	if(err==-1)
		return -errno;

	return ((errno=0),fd);
}

void*shmfree(void*shm)
{ 
	/**
	frees a shm that was created
	by either: shmalloc(), shmcopy(),
	shmview(), shmtake(), shmtakecopy(),
	shmrecv(), shmattach().
	**/
	int fd;
	fd = shm_shmfd(shm);
	munmap(shm-SHM_PGSZ, SHM_PGSZ+shmsize(shm));
	close(fd);

	return ((errno=0),NULL);
}
void*shmalloc(size_t size)
{
	/**
	allocates a new shm of length
	@size bytes.
	**/
	int err;

	if(size==0)
		return NULL;

	int fd;
	char tmp[8192]; 
	for(err=0;err<128;++err)
	{
#if 1
		snprintf(tmp,8192,"/dev/shm/shmio-%03x",err);
#else
		snprintf(tmp,8192,"/tmp/shmio-%03x",err);
#endif
		fd = open(tmp,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
		if(fd!=-1)
			goto pass;
	}
	return NULL; 

pass:
	err = unlink(tmp); 
	if(err<0)
		goto fail;
	err = ftruncate(fd,size);
	if(err<0)
		goto fail;

	void*shm;
	shm=shmattach(fd,NULL);
	if(shm==NULL)
		goto fail;

	return ((errno=0),shm);

fail:
	err=errno;
		close(fd);
	errno=err;
	return NULL;
}
void*shmcopy(void*shm)
{
	/**
	does a deep-copy of the
	shm.  the new shm is not
	shared with the old one,
	changes made to either 
	will not be reflected in
	the other.  either shm
	can be shared with other
	processes, and either shm
	can be shmfree()ed without
	corrupting the other shm.
	**/
	size_t size;
	size = shmsize(shm);

	void*copy;
	copy = shmalloc(size);
	if(copy==NULL)
		return NULL;

	memcpy(copy,shm,size);
	return ((errno=0),copy);
}
void*shmview(void*shm)
{
	/**
	does a shallow-copy of the
	shm.  the new shm is shared
	with the old one.  changes
	to one are reflected in the
	other.  either shm can be
	shared with other processes,
	and either shm can be 
	shmfree()ed without corrupting
	the remaining shm.
	**/
	int fd;
	fd = dup(shm_shmfd(shm));
	if(fd<0)
		return NULL;

	int err;
	err = fcntl(fd, F_SETFD, FD_CLOEXEC);	
	if(err<0)
		goto fail;

	shm = shmattach(fd,NULL);
	if(shm==NULL)
		goto fail;

	return ((errno=0),shm);

fail:
	err=errno;
		close(fd);
	errno=err;
	return NULL;
} 

int shmgive(int fd, void*shm)
{
	/**
	gives a shm to a process over
	a duplex socket.  the shm no
	longer exists in the sending
	process-- it is effectively
	shmfree()ed.
	**/
	int payload;
	payload = dup(shm_shmfd(shm));
	if(payload<0)
		return -errno;

	int err;
	err = shm_give(fd,payload);
	if(err<0)
	{
		err = errno;
			close(payload);
		errno = err;
		return -errno;
	}

	shmfree(shm);
	return ((errno=0),0);
}
void*shmtake(int fd, size_t*size)
{
	/**
	takes a shm that was given by
	another process over a duplex
	socket.  the shm may or may
	not also exist in the sending
	process, depending on whether
	the shm was sent with shmgive()
	or shmshare().
	**/
	int payload;
	payload = shm_take(fd,O_CLOEXEC);
	if(payload<0)
		return NULL; 

	void*shm;
	shm = shmattach(payload,NULL);
	if(shm==NULL)
		goto fail;

	if(size!=NULL) 
		*size = shmsize(shm);

	return ((errno=0),shm);

	int err;
fail:
	err=errno;
		close(payload);
	errno=err;
	return NULL;
} 
int shmshare(int fd, void*shm)
{
	/**
	shares a shm with another
	process over a duplex socket.
	the shm still exists in the
	sharing process when this
	function has returned, unlike
	shmgive().
	**/
	int payload;
	payload = dup(shm_shmfd(shm));
	if(payload<0)
		return payload; 

	int err;
	err = shm_give(fd,payload);
	if(err<0)
		goto fail;

	return ((errno=0),0);

fail:
	err=errno;
		close(payload);
	errno=err;
	return -errno;
} 
int shmgivecopy(int fd, void*shm)
{
	/**
	gives a *copy* of a shm
	to another process over a
	duplex socket.  the shm
	still exists in the giving
	process, but it is not shared
	with the given shm.  changes
	to one are not reflected in
	the other.
	**/
	void*copy;
	copy = shmcopy(shm);
	if(copy==NULL)
		return -errno;

	int err;
	err = shmgive(fd, copy);
	if(err<0)
		return -errno;

	return ((errno=0),0);
}
void* shmtakecopy(int fd, size_t*size)
{
	/**
	takes a *copy* of a shm
	to from process over a
	duplex socket.  if the shm
	still exists in the giving
	process, it is not shared
	with the taken shm.  changes
	to one are not reflected in
	the other.
	**/ 
	void*shm;
	shm = shmtake(fd,size);
	if(shm==NULL)
		return NULL;

	void*copy;
	copy = shmcopy(shm);

	/* free the taken shm even if copy failed */
	shmfree(shm);

	return ((errno=0),copy);
}

int shmwrite(int fd, void*shm)
{
	/**
	writes the contents of a shm
	to a generic fd (fifo,socket,
	file,tty,etc).  the shm can
	be read by another process
	with shmread().  the format is
	 [1-9][0-9]*:<binary contents of shm>
	where the digits preceeding
	the : are a the number of bytes 
	in the binary data in decimal 
	format.  this is the RFCxxxx
	netstrings format.
	
	when writing shm to processes
	over a network, the caller is
	responsible for ensuring that
	the shm data is in the correct
	endian format.  
	**/
	size_t length;
	length = shmsize(shm);
	if(length==0)
		return -errno;

	char tmp[64];
	snprintf(tmp,64,"%lu:",(unsigned long)length);

	size_t err;
	err = shm_writeall(fd,(unsigned char*)tmp,strlen(tmp));
	if(err==0)
		return -errno;

	return ((errno=0),shm_writeall(fd,shm,length));
}
void* shmread(int fd, size_t*size)
{
	/**
	read continusously from fd until
	binary data written by shmwrite() 
	is detected.  creates and returns
	a new shm populated by this data,
	or NULL on error.
	**/
	size_t length;
	length = shm_readlength(fd);
	if(length==0)
		return NULL;

	void*shm;
	shm = shmalloc(length);
	if(shm==NULL)
		return NULL;

	size_t err;
	err = shm_readall(fd,shm,length);
	if(err==0)
		goto fail;

	if(size!=NULL)
		*size = length;

	return ((errno=0),shm);

fail:
	err = errno;
		shmfree(shm);
	errno = err;
	return NULL;
}

