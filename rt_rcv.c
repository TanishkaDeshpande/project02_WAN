#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "sendto_dbg.h"
#include "net_include.h"
#include "rt_protocol.h"

static void Usage(int argc, char *argv[]);
static void Print_help(void);

/* Global state */
static int server_sock;
static int app_sock;
static struct sockaddr_in server_addr; // Store server address
static socklen_t server_addr_len = sizeof(server_addr);
static struct sockaddr_in app_addr;
static struct recv_buf_entry recv_buffer[RCV_BUF_SIZE];
static long base_delta_sec = 0;
static long base_delta_usec = 0;
static int base_delta_set = 0;
static int32_t lowest_undelivered_seq = 1;
static int32_t highest_received_seq = 0;

static long packets_received = 0;
static long packets_missed = 0;
static long total_delay = 0;

static void print_stats();
static void check_and_deliver_packets();
static void request_missing_packets();

/* Global configuration parameters (from command line) */
static int Loss_rate;
static char *Hostname;
static char *Server_Port_Str;
static char *App_Port_Str;
static int Latency_Window;

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    struct hostent *host_ent;
    int server_port, app_port;
    fd_set read_fds;
    struct timeval timeout;
    int ret;

    /* Initialize */
    Usage(argc, argv);
    sendto_dbg_init(Loss_rate);
    printf("Successfully initialized with:\n");
    printf("\tLoss rate       = %d\n", Loss_rate);
    printf("\tServer Hostname = %s\n", Hostname);
    printf("\tServer Port     = %s\n", Server_Port_Str);
    printf("\tApp Port        = %s\n", App_Port_Str);
    printf("\tLatency Window  = %d\n", Latency_Window);

    server_port = atoi(Server_Port_Str);
    app_port = atoi(App_Port_Str);

    /* Create and bind sockets */
    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        perror("server_sock socket");
        exit(1);
    }
    app_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (app_sock < 0) {
        perror("app_sock socket");
        exit(1);
    }

    host_ent = gethostbyname(Hostname);
    if (!host_ent) {
        fprintf(stderr, "Could not find host: %s\n", Hostname);
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr, host_ent->h_addr_list[0], host_ent->h_length);
    server_addr.sin_port = htons(server_port);

    memset(&app_addr, 0, sizeof(app_addr));
    app_addr.sin_family = AF_INET;
    app_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Deliver to localhost
    app_addr.sin_port = htons(app_port);

    /* Initialize receive buffer */
    for (int i = 0; i < RCV_BUF_SIZE; i++) {
        recv_buffer[i].received = 0;
        recv_buffer[i].delivered = 0;
        recv_buffer[i].nack_sent = 0; // Initialize nack_sent flag
    }

    /* Connection Handshake */
    struct ctrl_msg connect_msg;
    connect_msg.type = htonl(MSG_TYPE_CONNECT);
    connect_msg.seq = 0;
    sendto_dbg(server_sock, (char *)&connect_msg, sizeof(connect_msg), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    printf("Sent CONNECT to server. Waiting for ACCEPT...\n");

    FD_ZERO(&read_fds);
    FD_SET(server_sock, &read_fds);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    ret = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);
    if (ret > 0 && FD_ISSET(server_sock, &read_fds)) {
        struct ctrl_msg response;
        ret = recvfrom(server_sock, &response, sizeof(response), 0, (struct sockaddr *)&server_addr, &server_addr_len);
        if (ret > 0 && ntohl(response.type) == MSG_TYPE_ACCEPT) {
            printf("Connection ACCEPTED.\n");
        } else {
            fprintf(stderr, "Connection REJECTED or timed out.\n");
            exit(1);
        }
    } else {
        fprintf(stderr, "No response from server.\n");
        exit(1);
    }

    while(1) {
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms

        ret = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);

        if (ret > 0 && FD_ISSET(server_sock, &read_fds)) {
            char recv_buf[MAX_MESS_LEN];
            ret = recvfrom(server_sock, recv_buf, MAX_MESS_LEN, 0, NULL, NULL);
            if (ret > 0) {
                struct ctrl_msg *header = (struct ctrl_msg *)recv_buf;
                if (ntohl(header->type) == MSG_TYPE_DATA) {
                    struct stream_pkt *pkt = (struct stream_pkt *)(recv_buf + sizeof(struct ctrl_msg));
                    pkt->seq = ntohl(pkt->seq);
                    pkt->ts_sec = ntohl(pkt->ts_sec);
                    pkt->ts_usec = ntohl(pkt->ts_usec);
                    int32_t seq = ntohl(header->seq);
                    int index = seq % RCV_BUF_SIZE;

                    if (!recv_buffer[index].received) {
                        packets_received++;
                        recv_buffer[index].seq = seq;
                        gettimeofday(&recv_buffer[index].recv_ts, NULL);
                        memcpy(&recv_buffer[index].pkt, pkt, sizeof(struct stream_pkt));
                        recv_buffer[index].received = 1;
                        recv_buffer[index].delivered = 0;
                        recv_buffer[index].nack_sent = 0; // Reset on receive

                        if (!base_delta_set) {
                            base_delta_sec = recv_buffer[index].recv_ts.tv_sec - pkt->ts_sec;
                            base_delta_usec = recv_buffer[index].recv_ts.tv_usec - pkt->ts_usec;
                            base_delta_set = 1;
                            printf("Base delta calculated: %ld s, %ld us\n", base_delta_sec, base_delta_usec);
                        }

                        long send_sec = pkt->ts_sec;
                        long send_usec = pkt->ts_usec;

                        recv_buffer[index].delivery_ts.tv_sec = send_sec + base_delta_sec;
                        recv_buffer[index].delivery_ts.tv_usec = send_usec + base_delta_usec + (Latency_Window * 1000);
                        // Normalize timeval
                        recv_buffer[index].delivery_ts.tv_sec += recv_buffer[index].delivery_ts.tv_usec / 1000000;
                        recv_buffer[index].delivery_ts.tv_usec %= 1000000;
                        
                        if (seq > highest_received_seq) {
                            highest_received_seq = seq;
                        }

                        long delay = (recv_buffer[index].recv_ts.tv_sec - send_sec) * 1000000 + (recv_buffer[index].recv_ts.tv_usec - send_usec);
                        total_delay += delay;
                    }
                }
            }
        }

        check_and_deliver_packets();
        request_missing_packets();

        static struct timeval last_stats_time = {0, 0};
        struct timeval now;
        gettimeofday(&now, NULL);
        if (now.tv_sec - last_stats_time.tv_sec >= 5) {
            print_stats();
            last_stats_time = now;
        }
    }
    return 0;
}

static void check_and_deliver_packets() {
    struct timeval now;
    gettimeofday(&now, NULL);

    while (1) {
        int index = lowest_undelivered_seq % RCV_BUF_SIZE;
        if (recv_buffer[index].received && !recv_buffer[index].delivered) {
            if (now.tv_sec > recv_buffer[index].delivery_ts.tv_sec ||
                (now.tv_sec == recv_buffer[index].delivery_ts.tv_sec && now.tv_usec >= recv_buffer[index].delivery_ts.tv_usec)) {
                
                sendto(app_sock, &recv_buffer[index].pkt, sizeof(struct stream_pkt), 0, (struct sockaddr *)&app_addr, sizeof(app_addr));
                recv_buffer[index].delivered = 1;
                lowest_undelivered_seq++;
            } else {
                // Packet not ready for delivery
                break;
            }
        } else {
            // Packet not yet received
            // Check if we should consider it lost
            long deadline_sec = recv_buffer[index].delivery_ts.tv_sec;
            if (base_delta_set && (now.tv_sec > deadline_sec || (now.tv_sec == deadline_sec && now.tv_usec > recv_buffer[index].delivery_ts.tv_usec))) {
                 if (recv_buffer[index].seq == lowest_undelivered_seq) { // Make sure we are not checking an old packet
                    packets_missed++;
                    lowest_undelivered_seq++;
                 } else {
                    break; // Not the one we are looking for
                 }
            } else {
                break; // Not yet time to consider it lost
            }
        }
    }
}

static void request_missing_packets() {
    for (int32_t seq = lowest_undelivered_seq; seq < highest_received_seq; seq++) {
        int index = seq % RCV_BUF_SIZE;
        if (!recv_buffer[index].received && !recv_buffer[index].nack_sent) {
            struct ctrl_msg nack_msg;
            nack_msg.type = htonl(MSG_TYPE_NACK);
            nack_msg.seq = htonl(seq);
            sendto_dbg(server_sock, (char *)&nack_msg, sizeof(nack_msg), 0, (struct sockaddr *)&server_addr, server_addr_len);
            recv_buffer[index].nack_sent = 1; // Mark that we sent a NACK
        }
    }
}

static void print_stats() {
    printf("\n--- Receiver Stats ---\n");
    printf("Packets Received: %ld\n", packets_received);
    printf("Packets Missed: %ld\n", packets_missed);
    if (packets_received > 0) {
        printf("Avg One-Way Delay: %.2f ms\n", (double)total_delay / packets_received / 1000.0);
    }
    printf("Lowest Undelivered Seq: %d\n", lowest_undelivered_seq);
    printf("Highest Received Seq: %d\n", highest_received_seq);
    printf("----------------------\n\n");
}

/* Read commandline arguments */
static void Usage(int argc, char *argv[]) {

    if (argc != 5) {
        Print_help();
    }

    if (sscanf(argv[1], "%d", &Loss_rate) != 1) {
        Print_help();
    }

    Hostname = strtok(argv[2], ":");
    if (Hostname == NULL) {
        printf("Error: no hostname provided\n");
        Print_help();
    }
    Server_Port_Str = strtok(NULL, ":");
    if (Server_Port_Str == NULL) {
        printf("Error: no port provided\n");
        Print_help();
    }

    App_Port_Str = argv[3];

    if (sscanf(argv[4], "%d", &Latency_Window) != 1) {
        Print_help();
    }
}

static void Print_help(void) {
    printf("Usage: rt_rcv <loss_rate_percent> <server_hostname>:<server_port> <app_port> <latency_window>\n");
    exit(0);
}
