/*******************************************************/
/* easyUDPSockets.h */
/*******************************************************/

#define PORT 5000
#define GROUP "225.0.1.29"
#define MAXBUF 256

int easy_init(void);

int easy_send(char * message);

int easy_receive(char * buff);