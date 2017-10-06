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

#include "audioSimpleArgs.h"
#include "configureSndcard.h"

void record (int descSnd, const char *fileName, int fragmentSize);
void play (int descSnd, const char *fileName, int fragmentSize);


const int BITS_PER_BYTE = 8;

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

    int sndCardFormat;
    int channelNumber;
    int rate;
    int vol;
    int audioSimpleOperation;       /* record, play */
    int descriptorSnd;
    int requestedFragmentSize;

    /* we configure the signal */
    sigInfo.sa_handler = signalHandler;
    sigInfo.sa_flags = 0; 
    sigemptyset(&sigInfo.sa_mask); /* clear sa_mask values */
    if ((sigaction (SIGINT, &sigInfo, NULL)) < 0) {
        printf("Error installing signal, error: %s", strerror(errno)); 
        exit(1);
    }
    /* obtain values from the command line - or default values otherwise */
    if (-1 == args_capture (argc, argv, &audioSimpleOperation, &sndCardFormat, 
            &channelNumber, &rate, &vol, &requestedFragmentSize, &fileName))
    { exit(1);  /* there was an error parsing the arguments, the error type 
                   is printed by the args_capture function */
    };


    /* create snd descritor and configure soundcard to given format, rate, number of channels. 
     * Also configures fragment size */
    configSndcard (&descriptorSnd, &sndCardFormat, &channelNumber, &rate, &requestedFragmentSize); 
    vol = configVol (channelNumber, descriptorSnd, vol);

    /* obtained values -may differ slightly - eg. frequency - from requested values */
    args_print(audioSimpleOperation, sndCardFormat, channelNumber, rate, vol, requestedFragmentSize, fileName);
    printFragmentSize (descriptorSnd);
    printf ("Duration of each packet exchanged with the soundcard :%f\n", (float) requestedFragmentSize / (float) (channelNumber * sndCardFormat / BITS_PER_BYTE) * rate); /* note that in this case sndCardFormat is ALSO the number of bits of the format, this may not be the case */

    if (audioSimpleOperation == RECORD)
        record (descriptorSnd, fileName, requestedFragmentSize); /* this function - and the following functions - are coded in configureSndcard */
    else if (audioSimpleOperation == PLAY)
        play (descriptorSnd, fileName, requestedFragmentSize);
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
