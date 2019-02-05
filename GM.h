void process(int pnum);
void gmhandler(int signal,siginfo_t *psi,void *context);
void handler2(int signal,siginfo_t *psi,void *context);
int LFU(int pnum);

typedef struct page{
	int pageindex;
	int framenum;
	int offset;
	int accesscounter;
	int pnum;
	char rw; 
	short M; //flag for occupied page
	short req; //flag to check if current page was the one requested
}Page;
