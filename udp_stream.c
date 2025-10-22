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
static void isleep(int usec);

static char *Target_IP;
static int Port;
static int Num_msgs;

int main(int argc, char *argv[]) {
    int ss;                  /* socket for sending to destination */
    struct sockaddr_in send_addr; /* address to send to */
    struct hostent h_ent;
    struct hostent *p_h_ent;
    int host_num;
    struct stream_pkt send_pkt;
    struct timeval start_ts, now;
    long int duration;
    long int next_report = REPORT_SEC * 1000000;
    double rate;
    int int_delay;
    int ret;

    /* Initialize */
    Usage(argc, argv);
    printf("Successfully initialized with:\n");
    printf("\tTarget IP = %s\n", Target_IP);
    printf("\tPort = %d\n", Port);
    printf("\tMessages to send = %d\n", Num_msgs);

    /* Set up socket for sending to destination */
    ss = socket(AF_INET, SOCK_DGRAM, 0);
    if (ss < 0) {
        perror("error opening sending socket");
        exit(1);
    }   
    
    /* Set up address to send to */
    p_h_ent = gethostbyname(Target_IP);
    if (p_h_ent == NULL) { 
        printf("gethostbyname error.\n");
        exit(1);
    }   

    memcpy(&h_ent, p_h_ent, sizeof(h_ent));
    memcpy(&host_num, h_ent.h_addr_list[0], sizeof(host_num));

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = host_num; 
    send_addr.sin_port = htons(Port);

    /* Send udp packet stream */
    gettimeofday(&start_ts, NULL);
    for (send_pkt.seq = 1; send_pkt.seq <= Num_msgs; send_pkt.seq++)
    {
        gettimeofday(&now, NULL);
        send_pkt.ts_sec = now.tv_sec;
        send_pkt.ts_usec = now.tv_usec;

        /* send the packet */
        ret = sendto(ss, (char *)&send_pkt, sizeof(send_pkt), 0,
                (struct sockaddr*)&send_addr, sizeof(send_addr));
        if (ret < sizeof(send_pkt)) {
            printf("Error sending\n");
        }

        /* calculate current sending rate, sleep if needed */
        duration = now.tv_sec - start_ts.tv_sec;
        duration *= 1000000;
        duration += now.tv_usec - start_ts.tv_usec;
        rate = sizeof(send_pkt) * send_pkt.seq * 8; /* bits sent so far */
        rate = rate / duration; /* bits per usec == megabits per sec */

        if (duration > next_report) {
            printf("\n%lf sec elapsed\n", duration / 1000000.0);
            printf("Sent:        %d pkts\n", send_pkt.seq);
            printf("Avg rate:    %lf Mbps\n", rate);
            next_report += REPORT_SEC * 1000000;
        }

        if (rate > TARGET_RATE) {
            int_delay = sizeof(send_pkt) * send_pkt.seq * 8; /* bits sent so far */
            int_delay = int_delay / TARGET_RATE; /* how long *should* it have taken to send that many bits */
            int_delay = int_delay - duration; /* how much do we need to wait to make up the difference */

            if (int_delay > 0) {
                isleep(int_delay);
            }
        }
    }

    return 0;
}

static void isleep(int usec)
{
    int diff;
    struct timeval start, now;
    struct timezone tz; 
  
    gettimeofday(&start, &tz);    
    diff = 0;
    while(diff < usec) {
        /* If enough time to sleep, otherwise, busywait */
        if(usec - diff > 200) {
            usleep(usec-20);
        }   
        gettimeofday(&now, &tz);
        diff = now.tv_sec - start.tv_sec;
        diff *= 1000000;
        diff += now.tv_usec - start.tv_usec;
    }
}

/* Read commandline arguments */
static void Usage(int argc, char *argv[]) {
    char *port_str;

    if (argc != 3) {
        Print_help();
    }

    Target_IP = strtok(argv[1], ":");
    if (Target_IP == NULL) {
        printf("Error: incorrect target IP and port format\n");
        Print_help();
    }
    port_str = strtok(NULL, ":");
    if (port_str == NULL) {
        printf("Error: incorrect target IP and port format\n");
        Print_help();
    }
    Port = atoi(port_str);

    if (sscanf(argv[2], "%d", &Num_msgs) != 1) {
        Print_help();
    }
}

static void Print_help(void) {
    printf("Usage: udp_stream <ip_addr>:<port> <num_msgs>\n");
    exit(0);
}
