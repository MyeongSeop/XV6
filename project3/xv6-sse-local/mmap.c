//#include "stat.h"
#include "types.h"
#include "user.h"

int main(int argc, char *argv[]){
	if(argc!= 7){
		printf(1, "Wrong input!\n");
		exit();
	}
	int addr=atoi(argv[1]);
	//uint addr = addr_i;
	int length = atoi(argv[2]);
	int prot = atoi(argv[3]);
	int flags;
	if(argv[4][0]=='-'){
		if(argv[4][1]=='1') flags=-1;
		else {
			printf(1, "Wrong flag!\n");
			exit();
		}
	}
	else flags= atoi(argv[4]);
	int fd = atoi(argv[5]);
	int offset = atoi(argv[6]);
	mmap(addr, length, prot, flags, fd, offset);
	exit();
}
