#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h> 
#include <netdb.h>
#include <sys/time.h>
#include "udp_stream_private.h"

static void Usage(int argc, char *argv[]);
static void Print_help(void);
static int Cmp_time(struct timeval t1, struct timeval t2);

static const struct timeval Report_Interval = {5, 0};

static int Port;

int main(int argc, char *argv[]) {
    int sr;                  /* socket for receiving */
    struct sockaddr_in name; /* address to bind to receive */
    fd_set read_mask, tmp_mask;
    int rcvd_bytes;
    struct stream_pkt rcv_pkt;
    struct timeval start_ts, now;
    long int duration;
    double rate;
    int total_bytes;
    int rcvd_count;
    int last_rcvd;
    int num;
    double max_oneway;
    double oneway;
    struct timeval next_report_time;
    struct timeval timeout;
    struct timeval *to_ptr;

    /* Initialize */
    Usage(argc, argv);
    printf("Successfully initialized with:\n");
    printf("\tPort = %d\n", Port);

    /* Set up receiving socket */
    sr = socket(AF_INET, SOCK_DGRAM, 0);
    if (sr < 0) {
        perror("error opening input socket");
        exit(1);
    } 

    name.sin_family = AF_INET; 
    name.sin_addr.s_addr = INADDR_ANY; 
    name.sin_port = htons(Port);

    if (bind(sr, (struct sockaddr *)&name, sizeof(name)) < 0) { 
        perror("bind error");
        exit(1);
    }

    /* Set up masks to register sockets for select */
    FD_ZERO(&read_mask);
    FD_ZERO(&tmp_mask);
    FD_SET(sr, &read_mask); /* we want select to return when there is something
                               to read on sr */

    /* Init counts / reporting data */
    rcvd_count = 0;
    last_rcvd = 0;
    max_oneway = 0;
    total_bytes = 0;

    /* Begin event loop */
    for (;;)
    {
        /* reset mask */
        tmp_mask = read_mask;
        /* reset timeout. only need to report if we've actually received
         * something already */
        if (rcvd_count == 0) {
            to_ptr = NULL;
        } else {
            gettimeofday(&now, NULL);
            timersub(&next_report_time, &now, &timeout);
            to_ptr = &timeout;
        }

        num = select(FD_SETSIZE, &tmp_mask, NULL, NULL, to_ptr);
        if (num > 0) {
            if (FD_ISSET(sr, &tmp_mask)) {
                /* Receive data */
                rcvd_bytes = recvfrom(sr, &rcv_pkt, sizeof(rcv_pkt), 0, NULL,
                                      NULL);
                if (rcvd_bytes <= 0) {
                    printf("error receiving\n");
                    exit(1);
                }

                /* If first packet, start timing */
                if (rcvd_count == 0) {
                    gettimeofday(&start_ts, NULL);
                    timeradd(&start_ts, &Report_Interval, &next_report_time);
                }

                /* Update counts */
                total_bytes += rcvd_bytes;
                rcvd_count++;
                last_rcvd = rcv_pkt.seq;

                /* Calculate oneway delay */
                gettimeofday(&now, NULL);
                oneway = now.tv_sec - rcv_pkt.ts_sec;
                oneway *= 1000;
                oneway += (now.tv_usec - rcv_pkt.ts_usec) / 1000.0;
                if (oneway > max_oneway || rcvd_count == 1) {
                    max_oneway = oneway;
                }

            }
        }

        /* Check whether it is time to do reporting */
        gettimeofday(&now, NULL);
        if (rcvd_count > 0 && Cmp_time(now, next_report_time) > 0) {
            /* calculate current rate */
            gettimeofday(&now, NULL);
            duration = now.tv_sec - start_ts.tv_sec;
            duration *= 1000000;
            duration += now.tv_usec - start_ts.tv_usec;
            rate = total_bytes * 8; /* bits received so far */
            rate = rate / duration; /* bits per usec == megabits per sec */

            /* report */
            printf("\n%lf sec elapsed\n", duration / 1000000.0);
            printf("Avg rate:          %lf Mbps\n", rate);
            printf("Received:          %d pkts\n", rcvd_count);
            printf("Highest seq:       %d\n", last_rcvd);
            printf("Max oneway delay:  %lf ms\n", max_oneway);
            timeradd(&next_report_time, &Report_Interval, &next_report_time);
        }
    }

    return 0;
}

/* Returns 1 if t1 > t2, -1 if t1 < t2, 0 if equal */
static int Cmp_time(struct timeval t1, struct timeval t2) {
    if      (t1.tv_sec  > t2.tv_sec) return 1;
    else if (t1.tv_sec  < t2.tv_sec) return -1;
    else if (t1.tv_usec > t2.tv_usec) return 1;
    else if (t1.tv_usec < t2.tv_usec) return -1;
    else return 0;
}

/* Read commandline arguments */
static void Usage(int argc, char *argv[]) {
    if (argc != 2) {
        Print_help();
    }

    if (sscanf(argv[1], "%d", &Port) != 1) {
        Print_help();
    }
}

static void Print_help(void) {
    printf("Usage: udp_stream_rcv <port>\n");
    exit(0);
}
