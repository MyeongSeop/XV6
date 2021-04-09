#include "types.h"
#include "user.h"
#include "param.h"
#include "fcntl.h"

int main(int argc, char** argv) {
	/*printf(1, "mmap test \n");
	int i;
	int size = 8192;
	int fd = open("README", O_RDWR);
	//char* text = (char *)mmap(0, size, PROT_READ, MAP_POPULATE|MAP_PRIVATE, fd, 0);							  //File example
	char* text2 = (char *)mmap(8192, size, PROT_WRITE|PROT_READ, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);  //ANONYMOUS example
	//char* text = mmap((void *)4096, size, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_POPULATE|MAP_FIXED, fd, 0); // FIXED example
	
	for (i = 0; i < size; i++) 
		printf(1, "%c", text[i]);
	printf(1,"\n============file mmap end==========\n\n\n\n");

	text2[0] = 's';
	text2[4096] = 'Y';
	for (i = 0; i < size; i++) 
		printf(1, "%c", text2[i]);
	printf(1,"\n============anonymous mmap end==========\n");
	
	//munmap((uint)text);
	munmap((uint)text2);
	
	char* text3 = (char *)mmap(16384, size, PROT_READ, MAP_FIXED|MAP_POPULATE|MAP_PRIVATE, fd, 0);
	for (i = 0; i < size; i++) 
                printf(1, "%c", text3[i]);
        printf(1,"\n============file mmap end==========\n\n\n\n");
	int num=freemem();
	printf(1,"freemem:%d\n",num);
	//munmap((uint)text2);	
	munmap((uint)text3);
	num=freemem();
	printf(1,"freemem:%d\n",num);
	//check file not writable
	int fdd = open("README",O_RDONLY);
	char* text4 = (char *)mmap(0, 4096, PROT_WRITE|PROT_READ, MAP_POPULATE|MAP_PRIVATE, fdd, 0);
	for (i = 0; i < 4096; i++) 
                printf(1, "%c", text4[i]);
        printf(1,"\n============text4 mmap end==========\n");
	*/

	//fork test
	/*int fdd = open("README", O_RDWR);
	pid_t pid;
	pud=fork();
	if(pid>0){
		printf(1, "parent\n");
		char* text4 = (char *)mmap(0, size, PROT_WRITE|PROT_READ, 
	}
	
	exit();
	*/
	printf(1, "fork test\n");
        int i, size = 8192;
        int fd = open("README", O_RDWR);
        //int fdd = open("README", O_RDWR);
        char* text=(char *)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        char* text2;
	char* text3=(char *)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	printf(1,"check\n");
        int pid=fork();
	printf(1,"check2\n");
        if(pid==0){
		int fdd = open("README",O_RDWR);
                text2 = (char *)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_POPULATE, fdd, 0);
                text2[0]='F';
		text2[1]='U';
		text2[2]='C';
		text2[3]='K';
                printf(1,"child\n");
                printf(1,"parent_info\n");
                for(i=0;i<64;i++) printf(1,"%c",text[i]);
		printf(1,"\n-------------\nparent info 2\n");
		for(i=0;i<64;i++) printf(1,"%c",text3[i]);
                printf(1,"\n\nchild_info\n");
                for(i=0;i<64;i++) printf(1,"%c",text2[i]);
		printf(1,"\n-------------\n");
		munmap((uint)text2);
		exit();
        }
        else{
                wait();
                printf(1,"\nparent\n");
                for(i=0;i<64;i++) printf(1,"%c", text3[i]);
		printf(1,"\n----------------\n");
        }
        printf(1,"finish_1\n");

	int pid2=fork();
        printf(1,"check2\n");
        if(pid2==0){
                int fddd = open("README",O_RDWR);
                text2 = (char *)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_POPULATE, fddd, 0);
                text2[0]='F';
                text2[1]='U';
                text2[2]='C';
                text2[3]='K';
                printf(1,"child\n");
                printf(1,"parent_info\n");
                for(i=0;i<64;i++) printf(1,"%c",text[i]);
                printf(1,"\n-------------\nparent info 2\n");
                for(i=0;i<64;i++) printf(1,"%c",text3[i]);
                printf(1,"\n\nchild_info\n");
                for(i=0;i<64;i++) printf(1,"%c",text2[i]);
                printf(1,"\n-------------\n");
                munmap((uint)text2);
		exit();
        }
        else{
                wait();
                printf(1,"\nparent\n");
                for(i=0;i<64;i++) printf(1,"%c", text3[i]);
                printf(1,"\n----------------\n");
                munmap((uint)text);
                munmap((uint)text3);
        }
	printf(1,"finish 2\n");
        exit();

}
