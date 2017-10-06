#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "easyUDPSockets_2.h"

const char message[16]= "Sent from host2";
char buf[MAXBUF]; /* to receive data from remote node */

void main(int argc, char *argv[]){

	if(easy_init_2() < 0){
		printf("easy_init_2");
		exit(1);
	}

	if(easy_receive_2(buf) < 0){
		printf("easy_receive_2");
		exit(1);
	}

	if(easy_send_2(message) < 0){
		printf("easy_send_2");
		exit(1);
	}

}