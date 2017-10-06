#include <stdio.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <easyUDPSockets_1.h>

#define MAXBUF 256

const char message[16]= "Sent from host1"; /* 16 bytes is enough space for accomodating the message */
char buf[MAXBUF]; /* to receive data from remote node */

void main(int argc, char *argv[]){

	if(easy_init_1() < 0){
		printf("easy_init_1");
		exit(1);
	}

	if(easy_send_1(message) < 0){
		printf("easy_send_1");
		exit(1);
	}

	if(easy_receive_1(buf) < 0){
		printf("easy_receive_1");
		exit(1);
	}


}