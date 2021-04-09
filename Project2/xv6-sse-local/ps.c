#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
	if(argc!=2){
		printf(1,"Wrong input!\n");
		exit();
	}
	if(argv[1][0]=='-') exit();
	int val=atoi(argv[1]);
	ps(val);	
	exit();
}
	
