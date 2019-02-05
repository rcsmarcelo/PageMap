#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/types.h>
#include <wait.h>
#include <string.h>
#include <sys/time.h>
#include "VM.h"
#include "GM.h"
#include "sem.h"

#define PageSize 500
#define FrameSize 256
#define key 7095
#define ACCESS_MAX 12

static short references;
static Page **PhysMem;
static struct sigaction t, tp;
static int pids[4], pagefaults, busywrites, duration;
int semId;

int main(void) {
	struct timeval a, b;
	int status;
	initmems();
	pagefaults = busywrites = duration = 0;
	semId = semget (8752, 1, 0666 | IPC_CREAT);
	setSemValue(semId);
	gettimeofday(&a, NULL);
	if ((pids[0] = fork()) == 0) {
		tp.sa_sigaction = handler2;
		tp.sa_flags |= SA_SIGINFO;
		sigaction(SIGUSR2, &tp, NULL);
		process(1);
		exit(1);
	}
	else if ((pids[1]=fork()) == 0) {
		tp.sa_sigaction = handler2;
		tp.sa_flags |= SA_SIGINFO;
		sigaction(SIGUSR2, &tp, NULL);
		process(2);
		exit(1);
	}
	else if ((pids[2] = fork()) == 0) {
		tp.sa_sigaction = handler2;
		tp.sa_flags |= SA_SIGINFO;
		sigaction(SIGUSR2, &tp, NULL);
		process(3);
		exit(1);
	}
	if ((pids[3] = fork()) == 0) {
		tp.sa_sigaction = handler2;
		tp.sa_flags |= SA_SIGINFO;
		sigaction(SIGUSR2, &tp, NULL);
		process(4);
		exit(1);
	}
	else {
		references = 0;
		PhysMem = malloc(FrameSize * sizeof(Page*));
		for (int c = 0; c < FrameSize; c++) {
			PhysMem[c] = malloc(sizeof(Page));
			PhysMem[c]->framenum = PhysMem[c]->pageindex = PhysMem[c]->offset=-1;
			PhysMem[c]->M = PhysMem[c]->req = PhysMem[c]->accesscounter=0;
		}
		t.sa_sigaction = gmhandler;
		t.sa_flags |= SA_SIGINFO;
		sigaction(SIGUSR1, &t, NULL);
		for (int k = 0; k < 4; k++) {
			waitpid(pids[k], &status, 0);
			while (!WIFEXITED(status))
				waitpid(pids[k], &status, 0);
		}
		gettimeofday(&b, NULL);
		duration = (b.tv_sec - a.tv_sec);
		printf("---------------Simulation Details---------------\nDuration: %ds\nPagefaults: %d\nWrites on Busy Pages: %d\n", duration, pagefaults, busywrites);
	}
	return 0;

}

void handler2(int signal, siginfo_t *psi, void *context) {
	int pnum;
	if (getpid() == pids[0])
		pnum = 1;
	else if (getpid() == pids[1])
		pnum = 2;
	else if (getpid() == pids[2])
		pnum = 3;
	else if (getpid() == pids[3])
		pnum = 4;
	printf("Process %d lost a page!\n", pnum);
}

void gmhandler(int signal, siginfo_t *psi, void *context) {
	kill(psi->si_pid, SIGSTOP);
	pagefaults++;
	int pnum;
	if (psi->si_pid == pids[0])
		pnum = 1;
	else if (psi->si_pid == pids[1])
		pnum = 2;
	else if (psi->si_pid == pids[2])
		pnum = 3;
	else if (psi->si_pid == pids[3])
		pnum = 4;
	pnum = LFU(pnum);
	//if (pnum!=-1)
		//kill(pids[pnum-1], SIGUSR2);
	kill(psi->si_pid, SIGCONT);
}

void process(int pnum) {
	FILE *arq;
	char name[25];
	unsigned int addr, off, ind;
	char rw;
	switch (pnum) {
		case 1: strcpy(name, "compilador2.log"); break; 
		case 2: strcpy(name, "simulador2.log"); break; 
		case 3: strcpy(name, "compressor2.log"); break;
		case 4: strcpy(name, "matriz2.log"); break;
	}
	arq = fopen(name, "r");
	if (!arq) {
		puts("couldnt open file");
		exit(1);
	}
	while (fscanf(arq, "%x %c", &addr, &rw) != EOF) {
		ind = addr >> 16;
		off = addr & 0x0000FFFF;
		semaforoV(semId);
		int time = trans(pnum, ind, off, rw);
		semaforoP(semId);
		if (time>0) sleep(time);
	}
}

int LFU(int pnum) {
	Page *p; int i, index;
	int shmem = shmget(key+pnum-1, PageSize * sizeof(Page), S_IRWXU);
	p = shmat(shmem, 0, 0);
	for (i = 0; i < PageSize; i++)
		if (p[i].req)
			break;

	if (references%ACCESS_MAX == 0) //is true every access_maxth iteration
		eraseReferences(); //reset references
	references++;

	if (p[i].M) busywrites++;
	for (int c = 0; c < FrameSize; c++) {
		if (PhysMem[c]->pageindex == -1 && PhysMem[c]->offset == -1) { //frame is free
			p[i].framenum = c;
			p[i].req = 0;
			PhysMem[c] = &p[i];
			printf("New Mapping:P%d, F-%x+%x, %c\n", PhysMem[c]->pnum,PhysMem[c]->framenum, PhysMem[c]->offset, PhysMem[c]->rw);
			shmdt(p);
			return -1;
		}
	}
	//memory is full, need to replace a page
	int smallest = PhysMem[0]->accesscounter;
	for (int c = 0; c < FrameSize; c++) {
		if (( PhysMem[c]->accesscounter <= smallest) && (!PhysMem[c]->M)){ //find frame to replace
			index = c; smallest = PhysMem[c]->accesscounter;
		}
	}
	int ret = PhysMem[index]->pnum;
	p[i].framenum = index;
	p[i].req = 0;
	PhysMem[index] = &p[i]; //replace memory
	printf("New Mapping:P%d, F-%x+%x, %c\n", PhysMem[index]->pnum, PhysMem[index]->framenum, PhysMem[index]->offset, PhysMem[index]->rw);
	shmdt(p);
	shmem = shmget(key + ret - 1, PageSize * sizeof(Page), S_IRWXU);
	p = shmat(shmem, 0, 0);
	for (int c = 0; c < PageSize; c++)
		if (p[c].pageindex == PhysMem[index]->pageindex && p[c].offset == PhysMem[index]->offset) { //find page that was replaced in process's pagetable
			p[c].framenum = -1; //update reference
			p[c].req = 0;
		}
	shmdt(p);
	return ret;
}
