/* Unicast_multicast communication
THIS IS CODE for host1

compile with
	gcc -Wall -o host1 mcast_example_host1.c
execute as
	./host1
	
host1 uses an unicast local address.
host2 receives data in a multicast address. It receives from ANY sender.
(If you are testing with many of your mates, you should CHANGE the multicast address to avoid collisions)

host2 should be started first, then start host1. 
	packet1: host1 (unicast) -> host2(multicast)
	packet2: host2 (unicast) -> host1(unicast)
In all cases, PORT is used both as source and destination (just as stated in RFC 4961)

host1 and host2 can be executed in any host with multicast connectivity between them (preferrably from the same link, 163.117.144/24	
	
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>

#include "easyUDPSockets_1.h"

int sockId;
int result;     /* for storing results from system calls */
struct sockaddr_in localSAddr, remToSendSAddr, remToRecvSAddr; /* to build address/port info for local node and remote node */
socklen_t sockAddrInLength; /* for recvfrom */  

int easy_init_1(void) { 

    /* preparing bind */
    bzero(&localSAddr, sizeof(localSAddr));
    localSAddr.sin_family = AF_INET;
    localSAddr.sin_port = htons(PORT);
    localSAddr.sin_addr.s_addr = htonl (INADDR_ANY);


    if ((sockId = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("socket error\n");
        return -1; /* failure */
    }

    if (bind(sockId, (struct sockaddr *)&localSAddr, sizeof(struct sockaddr_in)) < 0) {
        printf("bind error\n");
        return -1; /* failure */
    }


    /* building structure to identify address/port of the remote node in order to send data to it */
    bzero(&remToSendSAddr, sizeof(remToSendSAddr));

    remToSendSAddr.sin_family = AF_INET;
    remToSendSAddr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, GROUP, &remToSendSAddr.sin_addr) < 0) {
        printf("inet_pton error\n");
        return -1; /* failure */
    }

    return 0; /* success */

}

int easy_send_1(char * message){

    /* Using sendto to send information. Since I've bind the socket, the local (source) port of the packet is fixed. In the rem structure I set the remote (destination) address and port */ 
    if ( (result = sendto(sockId, message, sizeof(message), /* flags */ 0, (struct sockaddr *) &remToSendSAddr, sizeof(remToSendSAddr)))<0) {
        printf("sendto error\n");
    } else {
        printf("Host1: Using sendto to send data to multicast destination\n"); 
    }

    return result;

}

int easy_receive_1(char * buff){

     /* we do not need to fill in the 'remToRecv' variable, but this structure appears with the address and port of the remote node from which the packet was received. 
       However, we need to provide in advance the maximum amount of memory which recvfrom can use starting from the 'remToRecv' pointer - to allow recvfrom to be sure that it does not exceed the available space. To do so, we need to provide the size of the 'remToRecv' variable */
    sockAddrInLength = sizeof (struct sockaddr_in); 

    buff[result] = 0;

    /* receives from any who wishes to send to host1 in this port */  
    if ((result= recvfrom(sockId, buff, MAXBUF, 0, (struct sockaddr *) &remToRecvSAddr, &sockAddrInLength)) < 0) {
        printf("recvfrom error\n");
    } else {
        buff[result] = 0;
        printf("Host1: Message received from unicast address. The message is: %s\n", buff); 
    }

    return result;

}