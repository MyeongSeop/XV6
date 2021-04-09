#include "types.h"
#include "user.h"
#include "param.h"
#include "fcmtl.h"

int main(int argc, char** argv){
	printf(1, "fork test\n");
	int i, size = 8192;
	int fd = open("README", O_RDWR);
	int fdd = open("README", O_RDWR);
	char* text=(char *)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_POPULATE, fd, 0);
	char* text2;
	int pid=fork();
	if(pid==0){
		text2 = (char *)mmap(8192, size, PROT_REAN|PROT_WRITE, MAP_PRIVATE|MAP_POPULATE|MAP_FIXED, fdd);
		text2[48]='g';
		printf(1,"child\n");
		printf(1,"parent_info\n");
		//for(i=0;i<64;i++) printf(1,"%c",text[i]);
		printf(1,"child_info\n");
		for(i=0;i<64;i++) printf(1,"%c",text2[i]);
	}
	else{
		wait();
		printf(1,"parent\n");
		for(i=0;i<64;i++) printf(1,"%c", text[i]);
	}
	printf(1"finisj\n");
	exit();

}
