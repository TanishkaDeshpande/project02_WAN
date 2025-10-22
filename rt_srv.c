// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/time.h>
// #include <arpa/inet.h>

// #include "sendto_dbg.h"
// #include "net_include.h"
// #include "rt_protocol.h"

// static void Usage(int argc, char *argv[]);
// static void Print_help(void);

// /* Global state */
// static int app_sock;
// static int client_sock;
// static struct sockaddr_in client_addr;
// static socklen_t client_addr_len = sizeof(client_addr);
// static int client_connected = 0;
// static struct send_buf_entry send_buffer[MAX_BUFFER_PACKETS];
// static int32_t highest_seq_num = 0;
// static long retransmissions = 0;
// static long data_sent = 0;
// static struct timeval start_time;

// static void print_stats();

// /* Global configuration parameters (from command line) */
// static int Loss_rate;
// static char *App_Port_Str;
// static char *Client_Port_Str;

// int main(int argc, char *argv[]) {
//     struct sockaddr_in app_addr, srv_addr;
//     int app_port, client_port;
//     fd_set read_fds;
//     struct timeval timeout;
//     int ret;

//     /* Initialize */
//     Usage(argc, argv);
//     sendto_dbg_init(Loss_rate);
//     printf("Successfully initialized with:\n");
//     printf("\tLoss rate   = %d\n", Loss_rate);
//     printf("\tApp Port    = %s\n", App_Port_Str);
//     printf("\tClient Port = %s\n", Client_Port_Str);

//     app_port = atoi(App_Port_Str);
//     client_port = atoi(Client_Port_Str);

//     /* Create and bind sockets */
//     app_sock = socket(AF_INET, SOCK_DGRAM, 0);
//     if (app_sock < 0) {
//         perror("app_sock socket");
//         exit(1);
//     }
//     client_sock = socket(AF_INET, SOCK_DGRAM, 0);
//     if (client_sock < 0) {
//         perror("client_sock socket");
//         exit(1);
//     }

//     memset(&app_addr, 0, sizeof(app_addr));
//     app_addr.sin_family = AF_INET;
//     app_addr.sin_addr.s_addr = htonl(INADDR_ANY);
//     app_addr.sin_port = htons(app_port);

//     memset(&srv_addr, 0, sizeof(srv_addr));
//     srv_addr.sin_family = AF_INET;
//     srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
//     srv_addr.sin_port = htons(client_port);

//     if (bind(app_sock, (struct sockaddr *)&app_addr, sizeof(app_addr)) < 0) {
//         perror("app_sock bind");
//         exit(1);
//     }
//     if (bind(client_sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
//         perror("client_sock bind");
//         exit(1);
//     }

//     /* Initialize send buffer */
//     for (int i = 0; i < MAX_BUFFER_PACKETS; i++) {
//         send_buffer[i].valid = 0;
//     }

//     gettimeofday(&start_time, NULL);

//     printf("Waiting for messages...\n");

//     while(1) {
//         FD_ZERO(&read_fds);
//         FD_SET(app_sock, &read_fds);
//         FD_SET(client_sock, &read_fds);

//         timeout.tv_sec = 5;
//         timeout.tv_usec = 0;

//         ret = select(FD_SETSIZE, &read_fds, NULL, NULL, &timeout);

//         if (ret < 0) {
//             perror("select");
//             exit(1);
//         } else if (ret == 0) {
//             // Timeout: print stats
//             if (client_connected) {
//                 print_stats();
//             }
//         } else {
//             if (FD_ISSET(app_sock, &read_fds)) {
//                 struct stream_pkt app_pkt;
//                 ret = recvfrom(app_sock, &app_pkt, sizeof(app_pkt), 0, NULL, NULL);
//                 if (ret > 0) {
//                     app_pkt.seq = htonl(app_pkt.seq);
//                     app_pkt.ts_sec = htonl(app_pkt.ts_sec);
//                     app_pkt.ts_usec = htonl(app_pkt.ts_usec);

//                     highest_seq_num++;
//                     int index = highest_seq_num % MAX_BUFFER_PACKETS;

//                     send_buffer[index].seq = highest_seq_num;
//                     gettimeofday(&send_buffer[index].send_ts, NULL);
//                     memcpy(&send_buffer[index].pkt, &app_pkt, sizeof(app_pkt));
//                     send_buffer[index].valid = 1;
//                     send_buffer[index].retransmitted = 0;

//                     if (client_connected) {
//                         struct ctrl_msg header;
//                         header.type = htonl(MSG_TYPE_DATA);
//                         header.seq = htonl(highest_seq_num);

//                         char send_buf[sizeof(header) + sizeof(app_pkt)];
//                         memcpy(send_buf, &header, sizeof(header));
//                         memcpy(send_buf + sizeof(header), &app_pkt, sizeof(app_pkt));

//                         sendto_dbg(client_sock, send_buf, sizeof(send_buf), 0, (struct sockaddr *)&client_addr, client_addr_len);
//                         data_sent += sizeof(send_buf);
//                     }
//                 }
//             }

//             if (FD_ISSET(client_sock, &read_fds)) {
//                 char recv_buf[MAX_MESS_LEN];
//                 struct sockaddr_in from_addr;
//                 socklen_t from_len = sizeof(from_addr);
//                 ret = recvfrom(client_sock, recv_buf, MAX_MESS_LEN, 0, (struct sockaddr *)&from_addr, &from_len);

//                 if (ret > 0) {
//                     struct ctrl_msg *header = (struct ctrl_msg *)recv_buf;
//                     int msg_type = ntohl(header->type);

//                     if (msg_type == MSG_TYPE_CONNECT) {
//                         struct ctrl_msg reply;
//                         if (!client_connected) {
//                             printf("Client connected.\n");
//                             client_connected = 1;
//                             memcpy(&client_addr, &from_addr, from_len);
//                             client_addr_len = from_len;
//                             reply.type = htonl(MSG_TYPE_ACCEPT);
//                             reply.seq = 0;
//                             sendto_dbg(client_sock, (char *)&reply, sizeof(reply), 0, (struct sockaddr *)&client_addr, client_addr_len);
//                             gettimeofday(&start_time, NULL); // Reset stats timer
//                         } else {
//                             printf("Connection rejected, already busy.\n");
//                             reply.type = htonl(MSG_TYPE_REJECT);
//                             reply.seq = 0;
//                             sendto_dbg(client_sock, (char *)&reply, sizeof(reply), 0, (struct sockaddr *)&from_addr, from_len);
//                         }
//                     } else if (msg_type == MSG_TYPE_NACK && client_connected) {
//                         int requested_seq = ntohl(header->seq);
//                         int index = requested_seq % MAX_BUFFER_PACKETS;

//                         if (send_buffer[index].valid && send_buffer[index].seq == requested_seq) {
//                             struct ctrl_msg data_header;
//                             data_header.type = htonl(MSG_TYPE_DATA);
//                             data_header.seq = htonl(requested_seq);

//                             char send_buf[sizeof(data_header) + sizeof(struct stream_pkt)];
//                             memcpy(send_buf, &data_header, sizeof(data_header));
//                             memcpy(send_buf + sizeof(data_header), &send_buffer[index].pkt, sizeof(struct stream_pkt));

//                             sendto_dbg(client_sock, send_buf, sizeof(send_buf), 0, (struct sockaddr *)&client_addr, client_addr_len);
//                             retransmissions++;
//                             send_buffer[index].retransmitted = 1;
//                         }
//                     }
//                 }
//             }
//         }
//     }
//     return 0;
// }

// static void print_stats() {
//     struct timeval now;
//     gettimeofday(&now, NULL);
//     double elapsed = (now.tv_sec - start_time.tv_sec) + (now.tv_usec - start_time.tv_usec) / 1000000.0;
//     if (elapsed == 0) return;

//     double rate = (data_sent * 8) / elapsed / 1000000.0; // Mbps

//     printf("\n--- Sender Stats ---\n");
//     printf("Time Elapsed: %.2f s\n", elapsed);
//     printf("Total Data Sent: %ld bytes\n", data_sent);
//     printf("Transfer Rate: %.2f Mbps\n", rate);
//     printf("Highest Seq Num: %d\n", highest_seq_num);
//     printf("Retransmissions: %ld\n", retransmissions);
//     printf("--------------------\n\n");
// }

// /* Read commandline arguments */
// static void Usage(int argc, char *argv[]) {
//     if (argc != 4) {
//         Print_help();
//     }

//     if (sscanf(argv[1], "%d", &Loss_rate) != 1) {
//         Print_help();
//     }

//     App_Port_Str = argv[2];
//     Client_Port_Str = argv[3];
// }

// static void Print_help(void) {
//     printf("Usage: rt_srv <loss_rate_percent> <app_port> <client_port>\n");
//     exit(0);
// }


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "sendto_dbg.h"
#include "net_include.h"
#include "rt_protocol.h"

static void Usage(int argc, char *argv[]);
static void Print_help(void);
static void print_stats(void);

/* ---------- Global state ---------- */
static int app_sock, client_sock;
static struct sockaddr_in client_addr;
static socklen_t client_addr_len = sizeof(client_addr);
static int client_connected = 0;

static struct send_buf_entry send_buffer[MAX_BUFFER_PACKETS];
static int32_t highest_seq_num = 0;
static long retransmissions = 0;
static long data_sent_bytes = 0;
static struct timeval start_time;

/* CLI args */
static int   Loss_rate;
static char *App_Port_Str;
static char *Client_Port_Str;

/* ---------- Helpers ---------- */
static inline void tv_normalize(struct timeval *tv) {
    while (tv->tv_usec >= 1000000) { tv->tv_sec++; tv->tv_usec -= 1000000; }
    while (tv->tv_usec < 0)        { tv->tv_sec--; tv->tv_usec += 1000000; }
}

int main(int argc, char *argv[])
{
    struct sockaddr_in app_addr, srv_addr;
    int app_port, client_port;
    fd_set read_fds;
    struct timeval timeout;
    int ret;

    Usage(argc, argv);
    sendto_dbg_init(Loss_rate);

    app_port   = atoi(App_Port_Str);
    client_port= atoi(Client_Port_Str);

    app_sock   = socket(AF_INET, SOCK_DGRAM, 0);
    client_sock= socket(AF_INET, SOCK_DGRAM, 0);
    if (app_sock < 0 || client_sock < 0) { perror("socket"); exit(1); }

    memset(&app_addr, 0, sizeof(app_addr));
    app_addr.sin_family = AF_INET;
    app_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    app_addr.sin_port = htons(app_port);

    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(client_port);

    if (bind(app_sock, (struct sockaddr*)&app_addr, sizeof(app_addr)) < 0 ||
        bind(client_sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    for (int i = 0; i < MAX_BUFFER_PACKETS; i++)
        send_buffer[i].valid = 0;

    gettimeofday(&start_time, NULL);
    printf("rt_srv ready. Listening on app:%d  client:%d\n", app_port, client_port);

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(app_sock, &read_fds);
        FD_SET(client_sock, &read_fds);

        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        ret = select(FD_SETSIZE, &read_fds, NULL, NULL, &timeout);
        if (ret < 0) { perror("select"); exit(1); }

        if (ret == 0) { /* periodic stats */
            if (client_connected) print_stats();
            continue;
        }

        /* ---------- From application ---------- */
        if (FD_ISSET(app_sock, &read_fds)) {
            struct stream_pkt pkt;
            ret = recvfrom(app_sock, &pkt, sizeof(pkt), 0, NULL, NULL);
            if (ret > 0) {
                /* Assign seq number and timestamp here */
                highest_seq_num++;
                int idx = highest_seq_num % MAX_BUFFER_PACKETS;

                gettimeofday(&send_buffer[idx].send_ts, NULL);

                pkt.seq     = htonl(highest_seq_num);
                pkt.ts_sec  = htonl(send_buffer[idx].send_ts.tv_sec);
                pkt.ts_usec = htonl(send_buffer[idx].send_ts.tv_usec);

                memcpy(&send_buffer[idx].pkt, &pkt, sizeof(pkt));
                send_buffer[idx].seq           = highest_seq_num;
                send_buffer[idx].valid         = 1;
                send_buffer[idx].retransmitted = 0;

                if (client_connected) {
                    struct ctrl_msg hdr;
                    hdr.type = htonl(MSG_TYPE_DATA);
                    hdr.seq  = htonl(highest_seq_num);

                    char buf[sizeof(hdr) + sizeof(pkt)];
                    memcpy(buf, &hdr, sizeof(hdr));
                    memcpy(buf + sizeof(hdr), &pkt, sizeof(pkt));

                    sendto_dbg(client_sock, buf, sizeof(buf), 0,
                               (struct sockaddr*)&client_addr, client_addr_len);
                    data_sent_bytes += sizeof(buf);
                }
            }
        }

        /* ---------- From receiver (connect/NACK) ---------- */
        if (FD_ISSET(client_sock, &read_fds)) {
            char recv_buf[MAX_MESS_LEN];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            ret = recvfrom(client_sock, recv_buf, sizeof(recv_buf), 0,
                           (struct sockaddr*)&from_addr, &from_len);
            if (ret <= 0) continue;

            struct ctrl_msg *hdr = (struct ctrl_msg*)recv_buf;
            int msg_type = ntohl(hdr->type);

            if (msg_type == MSG_TYPE_CONNECT) {
                struct ctrl_msg reply;
                if (!client_connected) {
                    client_connected = 1;
                    memcpy(&client_addr, &from_addr, from_len);
                    client_addr_len = from_len;

                    reply.type = htonl(MSG_TYPE_ACCEPT);
                    reply.seq  = 0;
                    sendto_dbg(client_sock, (char*)&reply, sizeof(reply), 0,
                               (struct sockaddr*)&client_addr, client_addr_len);
                    printf("Accepted new receiver.\n");
                    gettimeofday(&start_time, NULL);
                } else {
                    reply.type = htonl(MSG_TYPE_REJECT);
                    reply.seq  = 0;
                    sendto_dbg(client_sock, (char*)&reply, sizeof(reply), 0,
                               (struct sockaddr*)&from_addr, from_len);
                    printf("Rejected duplicate receiver.\n");
                }
            }
            else if (msg_type == MSG_TYPE_NACK && client_connected) {
                int req_seq = ntohl(hdr->seq);
                int idx = req_seq % MAX_BUFFER_PACKETS;

                if (send_buffer[idx].valid && send_buffer[idx].seq == req_seq) {
                    struct ctrl_msg data_hdr;
                    data_hdr.type = htonl(MSG_TYPE_DATA);
                    data_hdr.seq  = htonl(req_seq);

                    /* Use original packet (with original timestamp) */
                    char buf[sizeof(data_hdr) + sizeof(struct stream_pkt)];
                    memcpy(buf, &data_hdr, sizeof(data_hdr));
                    memcpy(buf + sizeof(data_hdr), &send_buffer[idx].pkt,
                           sizeof(struct stream_pkt));

                    sendto_dbg(client_sock, buf, sizeof(buf), 0,
                               (struct sockaddr*)&client_addr, client_addr_len);

                    retransmissions++;
                    send_buffer[idx].retransmitted = 1;
                }
            }
        }
    }
    return 0;
}

/* ---------- Stats ---------- */
static void print_stats(void)
{
    struct timeval now; gettimeofday(&now, NULL);
    double elapsed = (now.tv_sec - start_time.tv_sec)
                   + (now.tv_usec - start_time.tv_usec) / 1e6;
    if (elapsed <= 0) return;

    double mb_sent = data_sent_bytes / (1024.0 * 1024.0);
    double mbit_s  = (data_sent_bytes * 8.0) / (elapsed * 1e6);

    printf("\n----- Sender Report (rt_srv) -----\n");
    printf(" Elapsed: %.2fs\n", elapsed);
    printf(" Clean data sent: %.2f MB (%ld bytes)\n", mb_sent, data_sent_bytes);
    printf(" Avg rate: %.2f Mbit/s\n", mbit_s);
    printf(" Highest seq: %d\n", highest_seq_num);
    printf(" Retransmissions: %ld\n", retransmissions);
    printf("----------------------------------\n");
}

/* ---------- CLI ---------- */
static void Usage(int argc, char *argv[])
{
    if (argc != 4) Print_help();
    if (sscanf(argv[1], "%d", &Loss_rate) != 1) Print_help();
    App_Port_Str   = argv[2];
    Client_Port_Str= argv[3];
}

static void Print_help(void)
{
    printf("Usage: rt_srv <loss_rate_percent> <app_port> <client_port>\n");
    exit(0);
}