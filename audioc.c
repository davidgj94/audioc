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
*/

#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "audiocArgs.h"
#include "circularBuffer.h"
#include "configureSndcard.h"

void record (int descSnd, const char *fileName, int fragmentSize);
void play (int descSnd, const char *fileName, int fragmentSize);


const int BITS_PER_BYTE = 8;
const float MILI_PER_SEC = 1000.0;

/* only declare here variables which are used inside the signal handler */
char *buf = NULL;
char *fileName = NULL;     /* Memory is allocated by audioSimpleArgs, remember to free it */

/* activated by Ctrl-C */
void signalHandler (int sigNum __attribute__ ((unused)))  /* __attribute__ ((unused))   -> this indicates gcc not to show an 'unused parameter' warning about sigNum: is not used, but the function must be declared with this parameter */
{
    printf ("\naudioSimple was requested to finish\n");
    if (buf) free(buf);
    if (fileName) free(fileName);
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
    int descriptorSnd;
    int requestedFragmentSize;

    /****************************************
    new variables
     ***************************************/

    int multicastIp; /* 32-bit int containing the multicast addr. */
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

    /* we configure the signal */
    sigInfo.sa_handler = signalHandler;
    sigInfo.sa_flags = 0; 
    sigemptyset(&sigInfo.sa_mask); /* clear sa_mask values */
    if ((sigaction (SIGINT, &sigInfo, NULL)) < 0) {
        printf("Error installing signal, error: %s", strerror(errno)); 
        exit(1);
    }

    /****************************************
    old capture args
     ***************************************/

    // /* obtain values from the command line - or default values otherwise */
    // if (-1 == args_capture (argc, argv, &audioSimpleOperation, &sndCardFormat, 
    //         &channelNumber, &rate, &vol, &requestedFragmentSize, &fileName))
    // { exit(1);  /* there was an error parsing the arguments, the error type 
    //                is printed by the args_capture function */
    // };


    /****************************************
    new capture args
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
    float aux1 = ((float) packetDuration / MILI_PER_SEC) * (float) rate;
    int aux2 = (channelNumber * sndCardFormat / BITS_PER_BYTE);
    requestedFragmentSize = (int) aux1 * aux2;
    // printf("%d\n", (int) aux1);
    // printf("%d\n", aux2);
    // printf("%d\n", requestedFragmentSize);


    /* create snd descritor and configure soundcard to given format, rate, number of channels. 
     * Also configures fragment size */
    configSndcard (&descriptorSnd, &sndCardFormat, &channelNumber, &rate, &requestedFragmentSize); 
    vol = configVol (channelNumber, descriptorSnd, vol);
    printf("%d\n", requestedFragmentSize);

    /****************************************
    new args_print
     ***************************************/

    args_print_audioc(multicastIp, ssrc, port, packetDuration, payload, bufferingTime, vol, verbose);
    printFragmentSize (descriptorSnd);
    printf ("Duration of each packet exchanged with the soundcard :%f\n", (float) requestedFragmentSize / (float) (channelNumber * sndCardFormat / BITS_PER_BYTE) * rate);

    /****************************************
    old args_print
     ***************************************/

    // /* obtained values -may differ slightly - eg. frequency - from requested values */
    // args_print(audioSimpleOperation, sndCardFormat, channelNumber, rate, vol, requestedFragmentSize, fileName);
    // printFragmentSize (descriptorSnd);
    // printf ("Duration of each packet exchanged with the soundcard :%f\n", (float) requestedFragmentSize / (float) (channelNumber * sndCardFormat / BITS_PER_BYTE) * rate); /* note that in this case sndCardFormat is ALSO the number of bits of the format, this may not be the case */

    /****************************************
    old audioSimpleOperation chunk of code
     ***************************************/

    // if (audioSimpleOperation == RECORD)
    //     record (descriptorSnd, fileName, requestedFragmentSize);  this function - and the following functions - are coded in configureSndcard 
    // else if (audioSimpleOperation == PLAY)
    //     play (descriptorSnd, fileName, requestedFragmentSize);
};



/* creates a new file fileName. Creates 'fragmentSize' bytes from descSnd 
 * and stores them in the file opened.
 * If an error is found in the configuration of the soundcard, 
 * the process is stopped and an error message reported. */  
void record (int descSnd, const char * fileName, int fragmentSize)
{
    int file;
    int bytesRead;

    /* Creates buffer to store the audio data */

    buf = malloc (fragmentSize); 
    if (buf == NULL) { 
        printf("Could not reserve memory for audio data.\n"); 
        exit (1); /* very unusual case */ 
    }

    /* opens file for writing */
    if ((file = open  (fileName, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU)) < 0) {
        printf("Error creating file for writing, error: %s", strerror(errno));
        exit(1);
    }

    /* If it would be needed to store inside the file some data 
     * to represent the audio format, this should be the place */

    printf("Recording in blocks of %d bytes... :\n", fragmentSize);

    while (1) 
    { /* until Ctrl-C */
        bytesRead = read (descSnd, buf, fragmentSize); 
        if (bytesRead!= fragmentSize)
            printf ("Recorded a different number of bytes than expected (recorded %d bytes, expected %d)\n", bytesRead, fragmentSize);
        printf (".");fflush (stdout);

        bytesRead = write (file, buf, fragmentSize);
        if (bytesRead!= fragmentSize)
            printf("Written in file a different number of bytes than expected"); 
    }
}

/* This function opens an existing file 'fileName'. It reads 'fragmentSize'
 * bytes and sends them to the soundcard, for playback
 * If an error is found in the configuration of the soundcard, the process 
 * is stopped and an error message reported. */
void play (int descSnd, const char * fileName, int fragmentSize)
{
    int file;
    int bytesRead;

    /* Creates buffer to store the audio data */
    buf = malloc (fragmentSize); 
    if (buf == NULL) { printf("Could not reserve memory for audio data.\n"); exit (1); /* very unusual case */ }

    /* opens file in read-only mode */
    if ((file = open (fileName, O_RDONLY)) < 0) {
        printf("File could not be opened, error %s", strerror(errno));
        exit(1);
    }

    /* If you need to read from the file and process the audio format, this could be the place */

    printf("Playing in blocks of %d bytes... :\n", fragmentSize);

    while (1)
    { 
        bytesRead = read (file, buf, fragmentSize);
        if (bytesRead != fragmentSize)
            break; /* reached end of file */

        bytesRead = write (descSnd, buf, fragmentSize); 
        if (bytesRead!= fragmentSize)
            printf ("Played a different number of bytes than expected (recorded %d bytes, expected %d)\n", bytesRead, fragmentSize);
    }
};
