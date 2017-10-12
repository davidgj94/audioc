/*******************************************************/
/* easyUDPSockets.h */
/*******************************************************/

#define PORT 5000
// #define GROUP "225.0.1.29"
#define MAXBUF 512

int easy_init(struct in_addr multicastIp);

int easy_send(char * message, int fragmentSize);

int easy_receive(char * buff);