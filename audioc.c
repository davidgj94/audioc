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

void update_buffer(int descriptor, void *buffer, int size);
void play(int descriptor, int fragmentSize);
int ms2bytes(int duration, int rate, int channelNumber, int sndCardFormat);
// void * create_fill_cbuf(int numberOfBlocks, int BlockSize, int descriptor);

const int BITS_PER_BYTE = 8;
const float MILI_PER_SEC = 1000.0;

/* only declare here variables which are used inside the signal handler */
void *buf = NULL;
char *fileName = NULL;     /* Memory is allocated by audioSimpleArgs, remember to free it */
void *circular_buf = NULL;
/* activated by Ctrl-C */
void signalHandler (int sigNum __attribute__ ((unused)))  /* __attribute__ ((unused))   -> this indicates gcc not to show an 'unused parameter' warning about sigNum: is not used, but the function must be declared with this parameter */
{
    printf ("\naudioc was requested to finish\n");
    if (buf) free(buf);
    if (fileName) free(fileName);
    if (circular_buf) cbuf_destroy_buffer (circular_buf);
    exit (0);
}


void main(int argc, char *argv[])
{
    struct sigaction sigInfo; /* signal conf */
    int sndCardFormat;
    int channelNumber;
    int rate;
    int descriptorSnd;
    int requestedFragmentSize;
    unsigned int ssrc;          
    int port;         
    int vol;          
    int packetDuration;
    int verbose;       
    int payload;       
    int bufferingTime;  
    int numberOfBlocks;
    fd_set reading_set, writing_set;
    int sockId;
    struct in_addr multicastIp;
    int res;
    int buffering = 1;
    int i = 0;

    /* we configure the signal */
    sigInfo.sa_handler = signalHandler;
    sigInfo.sa_flags = 0; 
    sigemptyset(&sigInfo.sa_mask); /* clear sa_mask values */
    if ((sigaction (SIGINT, &sigInfo, NULL)) < 0) {
        printf("Error installing signal, error: %s", strerror(errno)); 
        exit(1);
    }


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
    
    requestedFragmentSize = ms2bytes(packetDuration, rate, channelNumber, sndCardFormat);
    numberOfBlocks = (int) ((float) ms2bytes(bufferingTime, rate, channelNumber, sndCardFormat) / (float) requestedFragmentSize);

    /* create snd descritor and configure soundcard to given format, rate, number of channels. 

     * Also configures fragment size */
    configSndcard (&descriptorSnd, &sndCardFormat, &channelNumber, &rate, &requestedFragmentSize); 
    vol = configVol (channelNumber, descriptorSnd, vol);

    args_print_audioc(multicastIp, ssrc, port, packetDuration, payload, bufferingTime, vol, verbose);
    printFragmentSize (descriptorSnd);
    printf ("Duration of each packet exchanged with the soundcard :%f\n", (float) requestedFragmentSize / (float) (channelNumber * sndCardFormat / BITS_PER_BYTE) * rate);


    /****************************************
    create circular buffer
     ***************************************/

    // circular_buf = cbuf_create_buffer (numberOfBlocks, requestedFragmentSize);

    /****************************************
    create no delay buffer
     ***************************************/

    buf = malloc (requestedFragmentSize); 
    if (buf == NULL) { 
        printf("Could not reserve memory for audio data.\n"); 
        exit (1); /* very unusual case */ 
    }

    if((sockId = easy_init(multicastIp)) < 0){
        printf("Could not initialize socket.\n");
        exit(1);
    }

    circular_buf = cbuf_create_buffer(numberOfBlocks, requestedFragmentSize);

    while(1){

        FD_ZERO(&reading_set);
        FD_SET(descriptorSnd, &reading_set);
        FD_SET(sockId, &reading_set);

        FD_ZERO(&writing_set);
        FD_SET(descriptorSnd, &writing_set);

        if ((res = select (FD_SETSIZE, &reading_set, &writing_set, NULL, NULL)) < 0) {
            printf("Select failed");
            exit(1);
        }else{

            if((FD_ISSET (descriptorSnd, &writing_set) == 1) && cbuf_has_block(circular_buf) && !buffering){
                circular_buf = cbuf_pointer_to_read (circular_buf);
                play(descriptorSnd, requestedFragmentSize);
            }

            if(FD_ISSET (descriptorSnd, &reading_set) == 1){
                update_buffer(descriptorSnd, buf, requestedFragmentSize);
                easy_send(buf, requestedFragmentSize);
            }

            if(FD_ISSET (sockId, &reading_set) == 1){
                circular_buf = cbuf_pointer_to_write (circular_buf);
                update_buffer(sockId, buf, requestedFragmentSize);
                memcpy(circular_buf, buf, requestedFragmentSize);

                if ((buffering = i < numberOfBlocks)){
                    i++;
                    printf("Buffering ...\n");
                    
                }
            }

        }

    }


};


void play(int descriptor, int fragmentSize){
    int bytesRead;
    bytesRead = write (descriptor, circular_buf, fragmentSize);
    if (bytesRead!= fragmentSize)
        printf ("Recorded a different number of bytes than expected (recorded %d bytes, expected %d)\n", bytesRead, fragmentSize);
    printf (".");fflush (stdout);
}

void update_buffer(int descriptor, void *buffer, int size){
    int bytesRead;
    bytesRead = read (descriptor, buffer, size);
    if (bytesRead!= size)
        printf ("Recorded a different number of bytes than expected (recorded %d bytes, expected %d)\n", bytesRead, size);
    printf ("*");fflush (stdout);
}

int ms2bytes(int duration, int rate, int channelNumber, int sndCardFormat){
    int numberOfSamples = (int) (
        ((float) duration / MILI_PER_SEC) * (float) rate);
    int bytesPerSample = channelNumber * sndCardFormat / BITS_PER_BYTE;
    return numberOfSamples * bytesPerSample;
}

// void * create_fill_cbuf(int numberOfBlocks, int BlockSize, int descriptor){
//     printf("Number of blocks is : %d\n", numberOfBlocks);
//     printf("Block size is : %d\n", BlockSize);
//     printf("%d\n",descriptor);

//     int i;
//     void *cbuf = NULL;

//     cbuf = cbuf_create_buffer (numberOfBlocks, BlockSize);
//     printf("Esperando para llenar el buffer circular ...\n");
//     for(i = 0; i < numberOfBlocks; i++){
//         cbuf = cbuf_pointer_to_write (cbuf);
//         update_buffer(descriptor, cbuf, BlockSize);
//         printf("Llenando buffer ...\n");
//     }

//     return cbuf;

// }

