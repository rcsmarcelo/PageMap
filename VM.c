#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <signal.h>
#include "VM.h"
#include "GM.h"
#include "sem.h"
#include <string.h>

#define MAXFILA 500

#define key 7095

int trans(int pnum, int index, int offset, char rw) {
	Page *table;
	int i;
	int seg = shmget(key +pnum - 1, MAXFILA * sizeof(Page), S_IRWXU);
	table = shmat(seg, 0, 0);
	/*convert logical to physical address*/
	for (i = 0; i < MAXFILA; i++) {
		if (table[i].pageindex == index && table[i].offset == offset) //check if address has already been read from .log
			break;
		else if (table[i].pageindex == -1) { //new address
			table[i].pageindex = index;
			table[i].offset = offset;
			table[i].rw = rw;
			break;
		}
	}
	if (rw == 'W') table[i].M = 1;
	else table[i].M = 0;
	table[i].accesscounter++;
	if (table[i].framenum == -1) { //current adress isnt mapped to memory
		table[i].req = 1;
		short boolean = table[i].M;
		shmdt(table);
		kill(getppid(), SIGUSR1); //pagefault
		if (boolean) return 2;
		else return 1;
	}
	printf("Already Mapped:P%d, F-%x+%x, %c\n", pnum, table[i].framenum, table[i].offset, table[i].rw);
	shmdt(table);
	return 0;
}

void initmems(void) {
	for (int c = 0; c < 4; c++) {
		int shmem = shmget(key + c, MAXFILA * sizeof(Page), IPC_CREAT | S_IRWXU);
		Page *p;
		p = shmat(shmem, 0, 0);
		for (int i = 0; i < MAXFILA; i++) {
			p[i].pageindex = p[i].offset = p[i].framenum =- 1;
			p[i].accesscounter = p[i].M = p[i].req = 0;
			p[i].pnum = c + 1;
		}
		shmdt(p);
	}
}

void eraseReferences(void) {
	for (int c = 0; c < 4; c++) {
		int shmem = shmget(key + c, MAXFILA *sizeof(Page), S_IRWXU);
		Page *p;
		p = shmat(shmem, 0, 0);
		for (int i = 0; i < MAXFILA; i++)
			p[i].accesscounter = 0;
		shmdt(p);
	}
}

