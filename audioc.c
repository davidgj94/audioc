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
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include "audiocArgs.h"
#include "circularBuffer.h"
#include "configureSndcard.h"
#include "easyUDPSockets.h"
#include "rtp.h"

void update_buffer(int descriptor, void *buffer, int size);
void play(int descriptor, void *buffer, int size, unsigned int * current_blocks);
int ms2bytes(int duration, int rate, int channelNumber, int sndCardFormat);
int detect_silence(void* buf, int fragmentSize, int sndCardFormat);
void insert_repeated_packets(void* circular_buf, void* buf, int requestedFragmentSize, unsigned int K, unsigned int numberOfBlocks, unsigned int * current_blocks);
void create_comfort_noise(void* noise_pointer, int fragmentSize, int sndCardFormat);
int check_write_cbuf(void* circular_buf, void* content_pointer, int size, unsigned int * current_blocks);
float get_diff_times(struct timeval* last_timeval, struct timeval* diff_times);
void reset_timer(int descriptorSnd, int rate, int channelNumber, int sndCardFormat, struct timeval* timer);

const int BITS_PER_BYTE = 8;
const float MILI_PER_SEC = 1000.0;
const float MICRO_PER_MILI = 1000.0;

const uint8_t ZERO_U8 = 128;
const uint8_t MA_U8 = 4;
const float PMA = 0.7;
const int SIZE_NOISE_U8 = 16;
const uint8_t NOISE_FRAGMENT_U8[] = {127,127,127,127,127,127,128,128,128,128,128,127,127,127,128,127};

/* only declare here variables which are used inside the signal handler */
void *buf_rcv = NULL;
void *buf_send = NULL;
void *bufheader = NULL;
char *fileName = NULL;     /* Memory is allocated by audioSimpleArgs, remember to free it */
void *circular_buf = NULL;
void *noise_pointer = NULL;


/* activated by Ctrl-C */
void signalHandler (int sigNum __attribute__ ((unused)))  /* __attribute__ ((unused))   -> this indicates gcc not to show an 'unused parameter' warning about sigNum: is not used, but the function must be declared with this parameter */
{
    printf ("\naudioc was requested to finish\n");
    if (buf_rcv) free(buf_rcv);
    if (buf_send) free(buf_send);
    if (fileName) free(fileName);
    if (circular_buf) cbuf_destroy_buffer (circular_buf);
    if (noise_pointer) free(noise_pointer);
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
    unsigned int nseq = 0;
    unsigned long int timeStamp = 0;
    unsigned long int timeStamp_anterior = 0;
    unsigned long int timeStamp_actual = 0;
    unsigned int seqNum_anterior = 0;
    unsigned int seqNum_actual = 0;
    unsigned long int ssrc = 0;
    rtp_hdr_t * hdr_message = NULL;
    char * audioData = NULL;
    unsigned int K = 0;
    unsigned int K_t = 0;
    int j_asdf = 0;
    unsigned int num_times_timer = 0;


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

    /* create snd descritor and configure soundcard to given format, rate, number of channels.

     * Also configures fragment size */
    configSndcard (&descriptorSnd, &sndCardFormat, &channelNumber, &rate, &requestedFragmentSize);

    numberOfBlocks = ms2bytes(bufferingTime + 200, rate, channelNumber, sndCardFormat) / requestedFragmentSize;
    printf("numberOfBlocks: %d\n", numberOfBlocks);

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

    circular_buf = cbuf_create_buffer(numberOfBlocks, requestedFragmentSize);

    noise_pointer = malloc (requestedFragmentSize);
    if (buf_send == NULL) {
        printf("Could not reserve memory for comfort noise.\n");
        exit (1); /* very unusual case */
    }
    create_comfort_noise(noise_pointer, requestedFragmentSize, sndCardFormat);

    // while(buffering){
    //     buffering = check_write_cbuf(circular_buf, noise_pointer, requestedFragmentSize);
    // }
    // exit(0);

    struct timeval last_timeval;
    struct timeval diff_times;
    if (gettimeofday (&last_timeval, NULL) <0) {
        printf("asdf\n");
    }

    printf ("El tiempo medido es %ld.%6ld\n", last_timeval.tv_sec, last_timeval.tv_usec); 


    unsigned int current_blocks = 0;
    unsigned int num_blocks_to_write;

    while(buffering){

        FD_ZERO(&reading_set);
        FD_SET(descriptorSnd, &reading_set);
        FD_SET(sockId, &reading_set);

        if ((res = select (FD_SETSIZE, &reading_set, NULL, NULL, NULL)) < 0) {
            printf("Select failed");
            exit(1);

        }else if(res == 0){
            printf("Nunca ocurre!!!!\n");
            buffering = 0;
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

                audioData = (char *)(hdr_message + 1);
    
                update_buffer(descriptorSnd, audioData, requestedFragmentSize);
                if(j_asdf%2){
                    printf("Impar -> mando\n");
                    easy_send(buf_send, requestedFragmentSize + sizeof(rtp_hdr_t));
                }else{
                    printf("Par -> no mando\n");
                }
                
                    
                nseq = nseq + 1;
                timeStamp = timeStamp + requestedFragmentSize;
                j_asdf++;
                       
            }

            if(FD_ISSET (sockId, &reading_set) == 1){
                printf("--------------------------------------------------------------------\n");
                printf("Buffering ...\n");

                update_buffer(sockId, buf_rcv, requestedFragmentSize + sizeof(rtp_hdr_t));

                hdr_message = (rtp_hdr_t *) buf_rcv;
                timeStamp_actual = ntohl((*hdr_message).ts);
                seqNum_actual = ntohs((*hdr_message).seq);

                audioData = (char *)(hdr_message + 1);

                printf("current_blocks: %d\n", current_blocks);
                printf("timeStamp_actual: %lu\n", timeStamp_actual);
                printf("seqNum_actual: %d\n", seqNum_actual);

                if(current_blocks == 0){
                    seqNum_anterior = seqNum_actual;
                    timeStamp_anterior = timeStamp_actual;
                    check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                }else if(seqNum_actual > seqNum_anterior){
                    K = seqNum_actual - seqNum_anterior;
                    printf("K: %d\n", K);
                    if(K == 1){
                        check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                    }else if(K < 4){
                        insert_repeated_packets(circular_buf, audioData, requestedFragmentSize, K - 1, numberOfBlocks, &current_blocks);
                        check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                    }else {
                        insert_repeated_packets(circular_buf, noise_pointer, requestedFragmentSize, K - 1, numberOfBlocks, &current_blocks);
                        check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                    }
                    seqNum_anterior = seqNum_actual;
                    timeStamp_anterior = timeStamp_actual;
                    buffering = (current_blocks < numberOfBlocks);
                }

                printf("timeStamp_anterior: %lu\n", timeStamp_anterior);
                printf("seqNum_anterior: %d\n", seqNum_anterior);
                printf("--------------------------------------------------------------------\n");
                

            }
            

        }

    }

    struct timeval silence_timer;
    silence_timer.tv_sec = 1;
    silence_timer.tv_usec = 0;
    while(1){

        FD_ZERO(&reading_set);
        FD_SET(descriptorSnd, &reading_set);
        FD_SET(sockId, &reading_set);

        FD_ZERO(&writing_set);
        if(cbuf_has_block(circular_buf)){
            printf("Hay espacio\n");
            FD_SET(descriptorSnd, &writing_set);
        }else{
            printf("NOOOOO Hay espacio\n");
        }

        if ((res = select (FD_SETSIZE, &reading_set, &writing_set, NULL, &silence_timer)) < 0) {
            printf("Select failed");
            exit(1);

        }else if(res == 0){
            printf("Rellenando con silencios\n");
            check_write_cbuf(circular_buf, noise_pointer, requestedFragmentSize, &current_blocks);
            num_times_timer++;

        }else{

            if(FD_ISSET (descriptorSnd, &writing_set) == 1){
                printf("Playing ...\n");
                play(descriptorSnd, cbuf_pointer_to_read (circular_buf), requestedFragmentSize, &current_blocks);
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

                audioData = (char *)(hdr_message + 1);
	
                update_buffer(descriptorSnd, audioData, requestedFragmentSize);

                if(detect_silence(audioData, requestedFragmentSize, sndCardFormat)){
                    if(get_diff_times(&last_timeval, &diff_times) < 10){
                        easy_send(buf_send, requestedFragmentSize + sizeof(rtp_hdr_t));
                        nseq = nseq + 1;
                    }
                }else{
                    easy_send(buf_send, requestedFragmentSize + sizeof(rtp_hdr_t));
                    nseq = nseq + 1;
                }

                
				timeStamp = timeStamp + requestedFragmentSize;
                       
            }

            if(FD_ISSET (sockId, &reading_set) == 1){
                printf("--------------------------------------------------------------------\n");
                printf("Writing in Circular buffer ...\n");

                update_buffer(sockId, buf_rcv, requestedFragmentSize + sizeof(rtp_hdr_t));

                hdr_message = (rtp_hdr_t *) buf_rcv;
                timeStamp_actual = ntohl((*hdr_message).ts);
		        seqNum_actual = ntohs((*hdr_message).seq);

                audioData = (char *)(hdr_message + 1);

                if(seqNum_actual > seqNum_anterior){

                    K = seqNum_actual - seqNum_anterior;
                    K_t = (unsigned int)(timeStamp_actual - timeStamp_anterior) / requestedFragmentSize;

                    printf("K: %d\n", K);
                    printf("K_t: %d\n", K_t);

                    if(K_t > 1){
                        if(K_t > num_times_timer){
                            num_blocks_to_write = K_t - num_times_timer - 1;
                        }else{
                            num_blocks_to_write = 0;
                        }
                    }

                    if(K == 1){

                        if(K_t == 1){
                            check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                        }else if(K_t > 1){
                            insert_repeated_packets(circular_buf, noise_pointer, requestedFragmentSize, num_blocks_to_write, numberOfBlocks, &current_blocks);
                            check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                        }
                        
                    }else if(K > 1){

                        if (K_t == K){

                            if(K_t < 4){
                                insert_repeated_packets(circular_buf, audioData, requestedFragmentSize, num_blocks_to_write, numberOfBlocks, &current_blocks);
                                check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                            }else {
                                insert_repeated_packets(circular_buf, noise_pointer, requestedFragmentSize, num_blocks_to_write, numberOfBlocks, &current_blocks);
                                check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                            }

                        }else if(K_t > K){
                            insert_repeated_packets(circular_buf, noise_pointer, requestedFragmentSize, num_blocks_to_write, numberOfBlocks, &current_blocks);
                            check_write_cbuf(circular_buf, audioData, requestedFragmentSize, &current_blocks);
                        }

                    }

                    num_times_timer = 0;
                    seqNum_anterior = seqNum_actual;
                    timeStamp_anterior = timeStamp_actual;

                    
                }

                printf("--------------------------------------------------------------------\n");

            }

        }

        reset_timer(descriptorSnd, rate, channelNumber, sndCardFormat, &silence_timer);


    }


};




void play(int descriptor, void *buffer, int size, unsigned int * current_blocks){
    int n_bytes;
    n_bytes = write (descriptor, buffer, size);
    if (n_bytes!= size){
        printf ("Different number of bytes ( %d bytes, expected %d)\n", n_bytes, size);
    }else{
        (*current_blocks)--;
    }
    // printf (".");fflush (stdout);
    printf("current_blocks: %d\n", *current_blocks);
}

void update_buffer(int descriptor, void *buffer, int size){
    int n_bytes;
    n_bytes = read (descriptor, buffer, size);
    if (n_bytes!= size) printf ("Different number of bytes ( %d bytes, expected %d)\n", n_bytes, size);
    // printf ("*");fflush (stdout);

}

int ms2bytes(int duration, int rate, int channelNumber, int sndCardFormat){
    int numberOfSamples = (int) (((float) duration / MILI_PER_SEC) * (float) rate);
    int bytesPerSample = channelNumber * sndCardFormat / BITS_PER_BYTE;
    return numberOfSamples * bytesPerSample;
}

void reset_timer(int descriptorSnd, int rate, int channelNumber, int sndCardFormat, struct timeval* timer){
    int numBytes;
    ioctl(descriptorSnd, SNDCTL_DSP_GETODELAY, &numBytes);
    int numberOfSamples = numBytes / (channelNumber * sndCardFormat / BITS_PER_BYTE);
    float bytesDuration = (float) numberOfSamples / (float) rate;

    bytesDuration = bytesDuration - 10 / MILI_PER_SEC;
    if(bytesDuration > 0){
        (*timer).tv_sec = (long) bytesDuration;
        (*timer).tv_usec = (long)((bytesDuration - (float) (*timer).tv_sec) * MILI_PER_SEC * MICRO_PER_MILI);
    }else{
        (*timer).tv_sec = 0;
        (*timer).tv_usec = 0;
        printf("Error bytesDuration < 0\n");
    }

    printf ("El timer sonara en %ld.%6ld\n", (*timer).tv_sec, (*timer).tv_usec);
    
}


void insert_repeated_packets(void* circular_buf, void* buf, int requestedFragmentSize, unsigned int K, unsigned int numberOfBlocks, unsigned int * current_blocks){
    unsigned int i;
    int inserted_block;

    printf("if es  %d\n", numberOfBlocks - (*current_blocks));
    if((numberOfBlocks - (*current_blocks)) < K){
        K = numberOfBlocks - (*current_blocks) - 1;
        printf("K_new es %d\n", K);
    }

    for(i=0; i<K; i++){
        inserted_block = check_write_cbuf(circular_buf, buf, requestedFragmentSize, current_blocks);
        if(inserted_block == 0){
            break;
        }
    }

}

void create_comfort_noise(void* noise_pointer, int fragmentSize, int sndCardFormat){

    int i;
    uint8_t* u8_pointer = (uint8_t*)noise_pointer;
    int16_t* s16_pointer = (int16_t*)noise_pointer;
    int size;

    if(sndCardFormat == U8){

        size = sizeof(uint8_t) * SIZE_NOISE_U8;
        for(i=0; i<fragmentSize; i= i + size){
            memcpy(u8_pointer, NOISE_FRAGMENT_U8, size);
            u8_pointer = u8_pointer + 1;
        }

    }else if(sndCardFormat == S16_LE){
        // Falta con 16 bits
    }

}


int detect_silence(void *buf, int fragmentSize, int sndCardFormat){

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
            if(((*u8_pointer) > (ZERO_U8 - MA_U8)) && ((*u8_pointer) < (ZERO_U8 + MA_U8))){
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
        printf("Silence detected\n");
    } 

    return (percentage_silence > PMA);
    
}

int check_write_cbuf(void* circular_buf, void* content_pointer, int size, unsigned int * current_blocks){
    void* to_write_pointer = cbuf_pointer_to_write (circular_buf);
    int inserted_block = 0;

    if (to_write_pointer != NULL){
        printf("insertar cbuf\n");
        memcpy(to_write_pointer, content_pointer, size);
        inserted_block = 1;
        (*current_blocks)++;
    }else{
        printf("cbuf lleno\n");
    }

    printf("current_blocks: %d\n", *current_blocks);

    return inserted_block;
}

float get_diff_times(struct timeval* last_timeval, struct timeval* diff_times){
    struct timeval current_timeval;
    float secs;
    float micro_secs;

    if (gettimeofday (&current_timeval, NULL) <0) {
        printf("asdf\n");
    }

    timersub(&current_timeval, last_timeval, diff_times);

    secs = (float) (*diff_times).tv_sec;
    micro_secs = ((float) (*diff_times).tv_usec) / MILI_PER_SEC;

    printf("La diferencia de tiempos es %f\n", secs + micro_secs);
    
    return secs + micro_secs;

}

