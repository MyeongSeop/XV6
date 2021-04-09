#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]){
	if(argc!=3){
		printf(1, "Wrong Input!\n");
		exit();
	}
	if(argv[1][0]=='-' || argv[2][0]=='-' || argv[1][0]=='0'){
		printf(1, "Wring Input!\n");
		exit();
	}
	int val=atoi(argv[2]);
	if(val<0 || val>39){
		printf(1, "Wrong Input!\n");
		exit();
	}
	setnice(atoi(argv[1]), atoi(argv[2]));
	exit();
}
