#include "types.h"
#include "user.h"

int main(int argc, char *argv[]){
	if(argc!=2){
		printf(1,"Wrong input!\n");
		exit();
	}
	int addr = atoi(argv[1]);
	munmap(addr);
	exit();

}
