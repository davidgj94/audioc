/*
audioSimple record|play [-b(8|16)] [stereo] [-vVOLUME] [-rRATE][-sBLOCK_SIZE]  fileName

Examples on how the program can be started:
./audioSimple record -b16 audioFile
./audioSimple record audioFile
./audioSimple play -b8 stereo -v90 -r8000 -s1024 audioFile

To compile, execute
gcc -Wall -Wextra -o audioSimple audioSimple.c audioSimpleArgs.c configureSndCard.c

Operations:
- play:     reads from a file and plays the content

-b 8 or 16 bits per sample
VOL volume [0..100]
RATE sampling rate in Hz

default values:  8 bits, vol 90, sampling rate 8000, 1 channel, 4096 bytes per block 

gcc -Wall -Wextra -o audioc audiocArgs.c circularBuffer.c configureSndcard.c easyUDPSockets.c audioc.c

./audioc 227.3.4.5 4532 -l100
*/

#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "audiocArgs.h"
#include "circularBuffer.h"
#include "configureSndcard.h"
#include "easyUDPSockets.h"

void record (int file, int fragmentSize);
void update_buffer(int descriptor, int fragmentSize);


const int BITS_PER_BYTE = 8;
const float MILI_PER_SEC = 1000.0;
const char *fileName_audio = "prueba.txt";

/* only declare here variables which are used inside the signal handler */
char *buf = NULL;
char *fileName = NULL;     /* Memory is allocated by audioSimpleArgs, remember to free it */
int descriptorSnd;
int file;
/* activated by Ctrl-C */
void signalHandler (int sigNum __attribute__ ((unused)))  /* __attribute__ ((unused))   -> this indicates gcc not to show an 'unused parameter' warning about sigNum: is not used, but the function must be declared with this parameter */
{
    printf ("\naudioSimple was requested to finish\n");
    if (buf) free(buf);
    if (fileName) free(fileName);
    close(descriptorSnd);
    close(file);
    exit (0);
}


void main(int argc, char *argv[])
{
    struct sigaction sigInfo; /* signal conf */

    /****************************************
    old variables
     ***************************************/

    int sndCardFormat;
    int channelNumber;
    int rate;
    //int vol;
    // int audioSimpleOperation;       /* record, play */
    
    int requestedFragmentSize;

    /****************************************
    new variables
     ***************************************/

    unsigned int ssrc;          /* Returns local SSRC value */
    int port;         /* Returns port to be used in the communication */ 
    int vol;          /* Returns volume requested (for both playing and 
                               recording). Value in range [0..100] */
    int packetDuration;/* Returns requested duration of the playout of an UDP packet.
                               Measured in ms */
    int verbose;       /* Returns if the user wants to show detailed traces (value 1) 
                               or not (value 0) */
    int payload;       /* Returns the requested payload for the communication. 
                               This is the payload to include in RTP packets (see enum payload). 
                               */
    int bufferingTime;  /* Returns the buffering time requested before starting playout.
                               Time measured in ms. */

    int numberOfBlocks;

    float aux1;
    int aux2;

    fd_set conjunto_lectura;
    int sockId;
    
    struct in_addr multicastIp;

    /* we configure the signal */
    sigInfo.sa_handler = signalHandler;
    sigInfo.sa_flags = 0; 
    sigemptyset(&sigInfo.sa_mask); /* clear sa_mask values */
    if ((sigaction (SIGINT, &sigInfo, NULL)) < 0) {
        printf("Error installing signal, error: %s", strerror(errno)); 
        exit(1);
    }

    
    /****************************************
    capture args
     ***************************************/

    /* obtain values from the command line - or default values otherwise */
    if (-1 == args_capture_audioc(argc, argv, &multicastIp, &ssrc, 
            &port, &vol, &packetDuration, &verbose, &payload, &bufferingTime))
    { exit(1);  /* there was an error parsing the arguments, the error type 
                   is printed by the args_capture function */
    };

    /****************************************
    get BITS_PER_BYTE, channelNumber, rate, requestedFragmentSize
     ***************************************/
    channelNumber = 1;

    if(payload == PCMU){
        rate = 8000;
        sndCardFormat = U8;
    }else if(payload == L16_1){
        rate = 44100;
        sndCardFormat = S16_LE;
    }
    printf("%d\n", packetDuration);
    aux1 = ((float) packetDuration / MILI_PER_SEC) * (float) rate;
    aux2 = (channelNumber * sndCardFormat / BITS_PER_BYTE);
    requestedFragmentSize = (int) aux1 * aux2;
    printf("%d\n", requestedFragmentSize);
    // printf("%d\n", (int) aux1);
    // printf("%d\n", aux2);
    // printf("%d\n", requestedFragmentSize);

    /* create snd descritor and configure soundcard to given format, rate, number of channels. 

     * Also configures fragment size */
    configSndcard (&descriptorSnd, &sndCardFormat, &channelNumber, &rate, &requestedFragmentSize); 
    vol = configVol (channelNumber, descriptorSnd, vol);
    printf("%d\n", requestedFragmentSize);

    /****************************************
    get numberOfBlocks
     ***************************************/

    aux1 = ((float) bufferingTime / MILI_PER_SEC) * (float) rate;
    aux2 = (channelNumber * sndCardFormat / BITS_PER_BYTE);
    int buffer_size_bytes = (int) aux1 * aux2;
    numberOfBlocks = (int)((float) buffer_size_bytes / (float) requestedFragmentSize);
    printf("%d\n", numberOfBlocks);

    /****************************************
    new args_print
     ***************************************/

    args_print_audioc(multicastIp, ssrc, port, packetDuration, payload, bufferingTime, vol, verbose);
    printFragmentSize (descriptorSnd);
    printf ("Duration of each packet exchanged with the soundcard :%f\n", (float) requestedFragmentSize / (float) (channelNumber * sndCardFormat / BITS_PER_BYTE) * rate);


    /****************************************
    create circular buffer
     ***************************************/

    //void * buffer = cbuf_create_buffer (numberOfBlocks, requestedFragmentSize);

    /****************************************
    create circular buffer
     ***************************************/
    //send(descriptorSnd, requestedFragmentSize);
    //record (descriptorSnd,"asdf.txt", requestedFragmentSize);

    buf = malloc (requestedFragmentSize); 
    if (buf == NULL) { 
        printf("Could not reserve memory for audio data.\n"); 
        exit (1); /* very unusual case */ 
    }

    /* opens file in read-only mode */
    if ((file = open (fileName_audio, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU)) < 0) {
        printf("File could not be opened, error %s", strerror(errno));
        exit(1);
    }

    sockId = easy_init(multicastIp);
    int res;

    while(1){
        FD_ZERO(&conjunto_lectura);
        FD_SET(descriptorSnd, &conjunto_lectura);
        FD_SET(sockId, &conjunto_lectura);
        if ((res = select (FD_SETSIZE, &conjunto_lectura, NULL, NULL, NULL)) <0) {
            printf("Fallo select");
        }else if(res==0){
            printf("Fallo timer");
        }else{
            if(FD_ISSET (descriptorSnd, &conjunto_lectura) == 1){
                update_buffer(descriptorSnd, requestedFragmentSize);
                easy_send(buf);
                printf("nuevo audio tarjeta");
            }

            if(FD_ISSET (sockId, &conjunto_lectura) == 1){
                update_buffer(sockId, requestedFragmentSize);
                record(descriptorSnd, requestedFragmentSize);
                printf("nuevo audio socket");

            }

        }

    }


    // while(1){
    //     update_buffer(descriptorSnd, requestedFragmentSize);
    //     record(file, requestedFragmentSize);
    //     printf("Grabando ...");
    // }
        





};



/* creates a new file fileName. Creates 'fragmentSize' bytes from descSnd 
 * and stores them in the file opened.
 * If an error is found in the configuration of the soundcard, 
 * the process is stopped and an error message reported. */  

void record (int file, int fragmentSize)
{
    int bytesRead;
    bytesRead = write (file, buf, fragmentSize);
    printf("%d\n", bytesRead);
    // if (bytesRead!= fragmentSize)
    //     printf ("Recorded a different number of bytes than expected (recorded %d bytes, expected %d)\n", bytesRead, fragmentSize);
    // printf (".");fflush (stdout);
}

void update_buffer(int descriptor, int fragmentSize){
    int bytesRead;
    bytesRead = read (descriptor, buf, fragmentSize);
    printf("%d\n", bytesRead);
    // if (bytesRead!= fragmentSize)
    //     printf ("Recorded a different number of bytes than expected (recorded %d bytes, expected %d)\n", bytesRead, fragmentSize);
    // printf (".");fflush (stdout);
}

/* This function opens an existing file 'fileName'. It reads 'fragmentSize'
 * bytes and sends them to the soundcard, for playback
 * If an error is found in the configuration of the soundcard, the process 
 * is stopped and an error message reported. */
// void play (int descSnd, const char * fileName, int fragmentSize)
// {
//     int file;
//     int bytesRead;

//     /* Creates buffer to store the audio data */
//     buf = malloc (fragmentSize); 
//     if (buf == NULL) { printf("Could not reserve memory for audio data.\n"); exit (1); /* very unusual case */ }

    

//      //If you need to read from the file and process the audio format, this could be the place 

//     printf("Playing in blocks of %d bytes... :\n", fragmentSize);

//     while (1)
//     { 
//         bytesRead = read (file, buf, fragmentSize);
//         if (bytesRead != fragmentSize)
//             break; /* reached end of file */

//         bytesRead = write (descSnd, buf, fragmentSize); 
//         if (bytesRead!= fragmentSize)
//             printf ("Played a different number of bytes than expected (recorded %d bytes, expected %d)\n", bytesRead, fragmentSize);
//     }
// };


