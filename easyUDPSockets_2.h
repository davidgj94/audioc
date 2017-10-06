/*******************************************************/
/* easyUDPSockets_2.h */
/*******************************************************/

#define PORT 5000
#define GROUP "225.0.1.29"
#define MAXBUF 256

int easy_init_2(void);

int easy_send_2(char * message);

int easy_receive_2(char * buff);