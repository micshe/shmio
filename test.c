#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<stdarg.h>

#include<sys/socket.h>
#include<sys/wait.h>

#include<unistd.h>

#include"shmio.h"

int crash(char*msg, ...)
{
	va_list args;
	va_start(args,msg);
	vfprintf(stderr,msg, args);
	va_end(args);
	fprintf(stderr,"\n");
	exit(1);
} 
int duplex(int fd[2]) { return ((errno=0),socketpair(AF_LOCAL,SOCK_STREAM,0,fd)); }

int test_givetake(void)
{
	int err;

	int pair[2];
	int status;
	pid_t pid;
	int*shm; 
	int i;

	err = duplex(pair);
	if(err==-1)
		crash("fail: duplex: %s\n",strerror(errno));

	pid = fork();
	if(pid==-1)
		crash("fail: fork: %s\n",strerror(errno)); 
	if(pid==0)
	{
		close(pair[0]);

		shm = shmalloc(16*sizeof(int));
		if(shm==NULL)
			crash("fail: shmalloc: %s\n",strerror(errno));

		for(i=0;i<16;++i)
			shm[i] = i*i;

		err = shmgive(pair[1],shm);
		if(err==-1)
			crash("fail: shmgive: %s\n",strerror(errno)); 
		printf("pass: shmgive\n");
	
		exit(0);
	}
	close(pair[1]);
	wait(&status);
	if(status==1)
		exit(1);

	shm = shmtake(pair[0],NULL);
	if(shm==NULL)
		crash("fail: shmtake: %s\n",strerror(errno)); 
	printf("pass: shmtake\n");

	for(i=0;i<16;++i)
		if(shm[i]!=i*i)
			crash("fail: recvd shm is corrupt\n");
	printf("pass: recvd shm is intact\n");

	close(pair[0]);
	shmfree(shm);

	return 0;
}

int test_readwrite(void)
{
	int err;

	int pair[2];
	int status;
	pid_t pid;
	int*shm;
	int i;

	/* shmread/shmwrite tests */

	err = duplex(pair);
	if(err==-1)
		crash("fail: duplex: %s\n",strerror(errno));

	pid=fork();
	if(pid==-1)
		crash("fail: fork");
	if(pid==0)
	{
		close(pair[0]);

		shm = shmalloc(16*sizeof(int));
		if(shm==NULL)
			crash("fail: subp: shmalloc: %s\n",strerror(errno));

		for(i=0;i<16;++i)
			shm[i] = i % 3;

		/* write misleading datastream */
		write(pair[1],"-:-:-:-:-",8);

		/* write shm datastream */
		shmwrite(pair[1],shm);

		exit(0);
	}
	close(pair[1]);	
	wait(&status);
	if(status==1)
		exit(1);

	shm = shmread(pair[0],NULL);
	if(shm==NULL)
		crash("fail: shmread: %s\n",strerror(errno));

	printf("debug: shmread() returned a shm of size %ld\n",(unsigned long)shmsize(shm));

	for(i=0;i<16;++i)
		if(shm[i] != i % 3)
			crash("fail: shmread read corrupt data\n");
	printf("pass: shmread read intact data\n");

	close(pair[0]);
	shmfree(shm); 

	return 0;
}

int test_zcb_givetake(int bufsz)
{
	int pair[2];
	int status;
	pid_t pid;
	int*shm;
	int i;

	int err;
	err = duplex(pair);
	if(err==-1)
		crash("fail: duplex: %s\n",strerror(errno));


	pid = fork();
	if(pid==-1)
		crash("fail: fork: %s\n",strerror(errno));
	if(pid==0)
	{
		close(pair[0]);

		shm = shmalloc(bufsz);
		if(shm==NULL)
			crash("subp: fail: shmalloc: %s\n",strerror(errno));
		printf("subp: debug: shmalloc'd a %lf megabyte buffer\n",
		       bufsz/(1024.0*1024.0));

		printf("subp: debug: populating %lf megabyte buffer...",
		       bufsz/(1024.0*1024.0));fflush(stdout);
		for(i=0;i<bufsz/(int)sizeof(int);++i)
			shm[i] = i*i;
		printf("done\n");

		printf("subp: debug: giving %lf megabyte buffer...",
		       bufsz/(1024.0*1024.0));fflush(stdout);
		shmgive(pair[1],shm);
		printf("done\n");

		exit(0);
	}
	close(pair[1]);

retry:
	((errno=0),wait(&status));
	if(errno==EINTR)
		goto retry;
	if(!WIFEXITED(status))
		crash("fail: subp failed to terminate\n");
	if(WEXITSTATUS(status) == EXIT_FAILURE)
		exit(EXIT_FAILURE);

	printf("debug: taking %lf megabyte buffer...",
	       bufsz/(1024.0*1024.0));fflush(stdout);
	shm = shmtake(pair[0],NULL);
	printf("done\n");

	for(i=0;i<bufsz/(int)sizeof(int);++i)
		if(shm[i]!=i*i)
			crash("fail: shmtake data is corrupt\n");
	printf("pass: shmtake data is intact\n");

	close(pair[0]);
	shmfree(shm);

	return 0;
}
int test_zcb_readwrite(int bufsz)
{
	int pair[2];
	int status;
	pid_t pid;
	int*shm;
	int i;

	int err;
	err = duplex(pair);
	if(err==-1)
		crash("fail: duplex: %s\n",strerror(errno)); 

	pid = fork();
	if(pid==-1)
		crash("fail: fork: %s\n",strerror(errno));
	if(pid==0)
	{
		close(pair[0]);

		shm = shmalloc(bufsz);
		if(shm==NULL)
			crash("subp: fail: shmalloc: %s\n",strerror(errno));
		printf("subp: debug: shmalloc'd a %lf megabyte buffer\n",
		       bufsz/(1024.0*1024.0));

		printf("subp: debug: populating %lf megabyte buffer...",
		       bufsz/(1024.0*1024.0));fflush(stdout);
		for(i=0;i<bufsz/(int)sizeof(int);++i)
			shm[i] = i*i;
		printf("done\n");

		printf("subp: debug: writing %lf megabyte buffer...", 
		       bufsz/(1024.0*1024.0));fflush(stdout);
		shmwrite(pair[1],shm);
		printf("done\n");

		exit(0);
	}
	close(pair[1]);

	printf("debug: reading %lf megabyte buffer...", 
	       bufsz/(1024.0*1024.0));fflush(stdout);
	shm = shmread(pair[0],NULL);
	if(shm==NULL)
		crash("\nfail: failed to shmread %lf megabyte buffer: %s\n",bufsz/(1024.0*1024.0),strerror(errno));
	printf("done\n");

//	printf("test: is shmread data intact\n");
	for(i=0;i<bufsz/(int)sizeof(int);++i)
		if(shm[i]!=i*i)
			crash("fail: shmread data is corrupt\n");
	printf("pass: shmread data is intact\n");

	close(pair[0]);
	shmfree(shm);

	wait(&status);
	if(status == 1)
		exit(1); 

	return 0;
}

int test_basic(void)
{
	int*shm;
	shm = shmalloc(16*sizeof(int));
	if(shm==NULL)
		crash("fail: failed to create 16*int shm: %s\n", strerror(errno));
	printf("pass: created 16*int shm\n");

	int i;
	for(i=0;i<16;++i)
		shm[i]=i;

	int*view;
	view = shmview(shm);
	if(view==NULL)
		crash("fail: failed to create a view of shm: %s\n", strerror(errno));
	else
		printf("pass: created view of 16*int shm\n");

	if(view==shm)
		crash("fail: failed to create a unique view of shm\n");

	for(i=0;i<16;++i)
		if(shm[i]!=i)
			crash("fail: view data is corrupt\n");
	printf("pass: view data is intact\n");

	for(i=0;i<16;++i)
		view[i] = 15-i;
	for(i=0;i<16;++i)
		if(shm[i]!=15-i)
			crash("fail: changes to view are not propogated to shm\n");
	printf("pass: changes to view are propogated to shm\n");

	int*copy;
	copy = shmcopy(shm);
	if(copy==NULL)
		crash("fail: failed to create a copy of shm: %s\n",strerror(errno));
	printf("pass: created a 16*int shm copy\n");

	for(i=0;i<16;++i)
		if(shm[i]!=15-i)
			crash("fail: copy data is corrupt\n");
	printf("pass: copy data is intact\n");

	for(i=0;i<16;++i)
		copy[i] = i;
	for(i=0;i<16;++i)
		if(shm[i]==i)
			crash("fail: changes to copy are propogated to shm\n");
	printf("pass: changes to copy are not propogated to shm\n");

	shmfree(copy);
	shmfree(view);

	for(i=0;i<16;++i)
		shm[i]=15-i;

	pid_t pid;
	pid=fork();
	if(pid==-1)
		crash("fail: fork: %s\n",strerror(errno));

	if(pid==0)
	{
		for(i=0;i<16;++i)
			if(shm[i]!=15-i)
				crash("fail: subprocess inherited corrupted shm\n");
		printf("pass: subprocess inherited intact shm\n");

		for(i=0;i<16;++i)
			shm[i]=i;

		exit(0);
	}
	int status;
	wait(&status);
	if(status==1)
		exit(0);

	for(i=0;i<16;++i)
		if(shm[i]!=i)
			crash("fail: subprocess's changes were not propogated to shm\n");
	printf("pass: subprocess's changes were propogated to shm\n");

	shmfree(shm);

	return 0;
}

int main(int argc, char*args[])
{
	int bufsz; 
	if(argc==1)
		bufsz = 128*1024*1024;
	else
		bufsz = strtod(args[1],NULL)*1024*1024;

	test_basic();
	printf("\n");
	test_givetake();
	/* TODO shmshare/shmgivecopy/shmtakecopy tests */ 
	printf("\n");
	test_readwrite(); 
	printf("\n");
	test_zcb_givetake(bufsz);
	printf("\n");
	test_zcb_readwrite(bufsz);

	return 0;
}

