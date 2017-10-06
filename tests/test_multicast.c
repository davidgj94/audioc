#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "easyUDPSockets.h"

const char message[16]= "Sent from host1"; /* 16 bytes is enough space for accomodating the message */
char buf[MAXBUF]; /* to receive data from remote node */

void main(int argc, char *argv[]){

	if(easy_init() < 0){
		printf("easy_init failed");
		exit(1);
	}

	while(1){
		if(easy_send(message) < 0){
			printf("easy_send failed");
			exit(1);
		}

		if(easy_receive(buf) < 0){
			printf("easy_receive failed");
			exit(1);
		}

	}

}