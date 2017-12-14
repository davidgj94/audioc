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
#include <stdint.h>

#include "audiocArgs.h"
#include "circularBuffer.h"
#include "configureSndcard.h"
#include "easyUDPSockets.h"
#include "rtp.h"

void update_buffer(int descriptor, void *buffer, int size);
void play(int descriptor, void *buffer, int size);
int ms2bytes(int duration, int rate, int channelNumber, int sndCardFormat);
void detect_silence(void* buf, int fragmentSize, int sndCardFormat);


// void * create_fill_cbuf(int numberOfBlocks, int BlockSize, int descriptor);

const int BITS_PER_BYTE = 8;
const float MILI_PER_SEC = 1000.0;

const uint8_t ZERO_U8 = 128;
const uint8_t MA_U8 = 4;
const float PMA = 0.7;

/* only declare here variables which are used inside the signal handler */
void *buf_rcv = NULL;
void *buf_send = NULL;
void *bufheader = NULL;
char *fileName = NULL;     /* Memory is allocated by audioSimpleArgs, remember to free it */
void *circular_buf = NULL;




/* activated by Ctrl-C */
void signalHandler (int sigNum __attribute__ ((unused)))  /* __attribute__ ((unused))   -> this indicates gcc not to show an 'unused parameter' warning about sigNum: is not used, but the function must be declared with this parameter */
{
    printf ("\naudioc was requested to finish\n");
    if (buf_rcv) free(buf_rcv);
    if (buf_send) free(buf_send);
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
    unsigned int nseq = 0;
    unsigned long int timeStamp = 0;
    unsigned long int timeStamp_anterior = 0;
    unsigned long int timeStamp_actual = 0;
    unsigned int seqNum_anterior = 0;
    unsigned int seqNum_actual = 0;
    unsigned long int ssrc = 0;
    rtp_hdr_t * hdr_message;


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


    /****************************************/


    buf_rcv = malloc (requestedFragmentSize + sizeof(rtp_hdr_t));
    if (buf_rcv == NULL) {
        printf("Could not reserve memory for buf_rcv.\n");
        exit (1); /* very unusual case */
    }

    buf_send = malloc (requestedFragmentSize + sizeof(rtp_hdr_t));
    if (buf_send == NULL) {
        printf("Could not reserve memory for buf_send.\n");
        exit (1); /* very unusual case */
    }


    if((sockId = easy_init(multicastIp)) < 0){
        printf("Could not initialize socket.\n");
        exit(1);
    }

    circular_buf = cbuf_create_buffer(numberOfBlocks + ms2bytes(200, rate, channelNumber, sndCardFormat), requestedFragmentSize);


    while(buffering){

        FD_ZERO(&reading_set);
        FD_SET(descriptorSnd, &reading_set);
        FD_SET(sockId, &reading_set);

        if ((res = select (FD_SETSIZE, &reading_set, NULL, NULL, NULL)) < 0) {
            printf("Select failed");
            exit(1);

        }else{

            if(FD_ISSET (descriptorSnd, &reading_set) == 1){
                printf("Sending ...\n");

                hdr_message = (rtp_hdr_t *) buf_send;

                (*hdr_message).version = 2;
                (*hdr_message).p = 0;
                (*hdr_message).x = 0;
                (*hdr_message).cc = 0;
                (*hdr_message).m = 0;
                (*hdr_message).m = 0;
                (*hdr_message).ssrc = htonl(ssrc);
                (*hdr_message).seq = htons(nseq);
                (*hdr_message).ts = htonl(timeStamp);
    
                update_buffer(descriptorSnd, buf_send + sizeof(rtp_hdr_t), requestedFragmentSize);

                detect_silence(buf_send + sizeof(rtp_hdr_t), requestedFragmentSize, sndCardFormat);

                easy_send(buf_send, requestedFragmentSize + sizeof(rtp_hdr_t));

                nseq = nseq + 1;
                timeStamp = timeStamp + requestedFragmentSize;
                       
            }

            if(FD_ISSET (sockId, &reading_set) == 1){
                printf("Writing in Circular buffer ...\n");

                update_buffer(sockId, buf_rcv, requestedFragmentSize + sizeof(rtp_hdr_t));

                hdr_message = (rtp_hdr_t *) buf_rcv;
                timeStamp_actual = ntohl((*hdr_message).ts);
                seqNum_actual = ntohs((*hdr_message).seq);

                if(i==0){
                    seqNum_anterior = seqNum_actual;
                    timeStamp_anterior = timeStamp_actual;
                    memcpy(cbuf_pointer_to_write (circular_buf), buf_rcv + sizeof(rtp_hdr_t), requestedFragmentSize);
                }else if((seqNum_actual == seqNum_anterior + 1) && (timeStamp_actual == timeStamp_anterior + requestedFragmentSize)){
                    seqNum_anterior = seqNum_actual;
                    timeStamp_anterior = timeStamp_actual;
                    memcpy(cbuf_pointer_to_write (circular_buf), buf_rcv + sizeof(rtp_hdr_t), requestedFragmentSize);
                }

                
                buffering = (++i < numberOfBlocks);
                printf("Buffering ...\n");

            }
            

        }

    }



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

            if((FD_ISSET (descriptorSnd, &writing_set) == 1) && cbuf_has_block(circular_buf)){
                printf("Playing ...\n");
                play(descriptorSnd, cbuf_pointer_to_read (circular_buf), requestedFragmentSize);
            }

            if(FD_ISSET (descriptorSnd, &reading_set) == 1){
                printf("Sending ...\n");

                hdr_message = (rtp_hdr_t *) buf_send;

                (*hdr_message).version = 2;
                (*hdr_message).p = 0;
                (*hdr_message).x = 0;
                (*hdr_message).cc = 0;
                (*hdr_message).m = 0;
                (*hdr_message).m = 0;
                (*hdr_message).ssrc = htonl(ssrc);
                (*hdr_message).seq = htons(nseq);
                (*hdr_message).ts = htonl(timeStamp);
	
                update_buffer(descriptorSnd, buf_send + sizeof(rtp_hdr_t), requestedFragmentSize);

                easy_send(buf_send, requestedFragmentSize + sizeof(rtp_hdr_t));

				nseq = nseq + 1;
				timeStamp = timeStamp + requestedFragmentSize;
                       
            }

            if(FD_ISSET (sockId, &reading_set) == 1){
                printf("Writing in Circular buffer ...\n");

                update_buffer(sockId, buf_rcv, requestedFragmentSize + sizeof(rtp_hdr_t));

                hdr_message = (rtp_hdr_t *) buf_rcv;
                timeStamp_actual = ntohl((*hdr_message).ts);
		        seqNum_actual = ntohs((*hdr_message).seq);

                if((seqNum_actual == seqNum_anterior + 1) && (timeStamp_actual == timeStamp_anterior + requestedFragmentSize)){
                    seqNum_anterior = seqNum_actual;
                    timeStamp_anterior = timeStamp_actual;
                    memcpy(cbuf_pointer_to_write (circular_buf), buf_rcv + sizeof(rtp_hdr_t), requestedFragmentSize);
                }


            }
        }

    }


};




void play(int descriptor, void *buffer, int size){
    int n_bytes;
    n_bytes = write (descriptor, buffer, size);
    if (n_bytes!= size)
        printf ("Different number of bytes ( %d bytes, expected %d)\n", n_bytes, size);
    // printf (".");fflush (stdout);
}

void update_buffer(int descriptor, void *buffer, int size){
    int n_bytes;
    n_bytes = read (descriptor, buffer, size);
    if (n_bytes!= size)
        printf ("Different number of bytes ( %d bytes, expected %d)\n", n_bytes, size);
    // printf ("*");fflush (stdout);

}

int ms2bytes(int duration, int rate, int channelNumber, int sndCardFormat){
    int numberOfSamples = (int) (
        ((float) duration / MILI_PER_SEC) * (float) rate);
    int bytesPerSample = channelNumber * sndCardFormat / BITS_PER_BYTE;
    return numberOfSamples * bytesPerSample;
}


void detect_silence(void *buf, int fragmentSize, int sndCardFormat){

    int i;
    uint8_t* u8_pointer = (uint8_t*)buf;
    int16_t* s16_pointer = (int16_t*)buf;
    int silence_frames = 0;
    int num_frames = 0;
    float percentage_silence;
    int size;


    if(sndCardFormat == U8){

        size = sizeof(uint8_t);
        for(i=0; i<fragmentSize; i= i + size){
            if((*u8_pointer > (ZERO_U8 - MA_U8)) && (*u8_pointer < (ZERO_U8 + MA_U8))){
                silence_frames = silence_frames + 1;
            }
            num_frames = num_frames + 1;
            u8_pointer = u8_pointer + 1;
        }

    }else if(sndCardFormat == S16_LE){
        // Falta con 16 bits
    }

    percentage_silence = (float)silence_frames / (float)num_frames;
    if(percentage_silence > PMA){
        printf("_");
    } 

    
}
