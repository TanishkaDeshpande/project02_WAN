// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <errno.h>
// #include <sys/time.h>
// #include <arpa/inet.h>
// #include <sys/time.h>

// #include "sendto_dbg.h"
// #include "net_include.h"
// #include "rt_protocol.h"

// static void Usage(int argc, char *argv[]);
// static void Print_help(void);

// /* Global state */
// static int server_sock;
// static int app_sock;
// static struct sockaddr_in server_addr; // Store server address
// static socklen_t server_addr_len = sizeof(server_addr);
// static struct sockaddr_in app_addr;
// static struct recv_buf_entry recv_buffer[RCV_BUF_SIZE];
// static long base_delta_sec = 0;
// static long base_delta_usec = 0;
// static int base_delta_set = 0;
// static int32_t lowest_undelivered_seq = 1;
// static int32_t highest_received_seq = 0;

// static long packets_received = 0;
// static long packets_missed = 0;
// static long total_delay = 0;

// static void print_stats();
// static void check_and_deliver_packets();
// static void request_missing_packets();

// /* Global configuration parameters (from command line) */
// static int Loss_rate;
// static char *Hostname;
// static char *Server_Port_Str;
// static char *App_Port_Str;
// static int Latency_Window;

// int main(int argc, char *argv[]) {
//     struct sockaddr_in server_addr;
//     struct hostent *host_ent;
//     int server_port, app_port;
//     fd_set read_fds;
//     struct timeval timeout;
//     int ret;

//     /* Initialize */
//     Usage(argc, argv);
//     sendto_dbg_init(Loss_rate);
//     printf("Successfully initialized with:\n");
//     printf("\tLoss rate       = %d\n", Loss_rate);
//     printf("\tServer Hostname = %s\n", Hostname);
//     printf("\tServer Port     = %s\n", Server_Port_Str);
//     printf("\tApp Port        = %s\n", App_Port_Str);
//     printf("\tLatency Window  = %d\n", Latency_Window);

//     server_port = atoi(Server_Port_Str);
//     app_port = atoi(App_Port_Str);

//     /* Create and bind sockets */
//     server_sock = socket(AF_INET, SOCK_DGRAM, 0);
//     if (server_sock < 0) {
//         perror("server_sock socket");
//         exit(1);
//     }
//     app_sock = socket(AF_INET, SOCK_DGRAM, 0);
//     if (app_sock < 0) {
//         perror("app_sock socket");
//         exit(1);
//     }

//     host_ent = gethostbyname(Hostname);
//     if (!host_ent) {
//         fprintf(stderr, "Could not find host: %s\n", Hostname);
//         exit(1);
//     }

//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sin_family = AF_INET;
//     memcpy(&server_addr.sin_addr, host_ent->h_addr_list[0], host_ent->h_length);
//     server_addr.sin_port = htons(server_port);

//     memset(&app_addr, 0, sizeof(app_addr));
//     app_addr.sin_family = AF_INET;
//     app_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Deliver to localhost
//     app_addr.sin_port = htons(app_port);

//     /* Initialize receive buffer */
//     for (int i = 0; i < RCV_BUF_SIZE; i++) {
//         recv_buffer[i].received = 0;
//         recv_buffer[i].delivered = 0;
//         recv_buffer[i].nack_sent = 0; // Initialize nack_sent flag
//     }

//     /* Connection Handshake */
//     struct ctrl_msg connect_msg;
//     connect_msg.type = htonl(MSG_TYPE_CONNECT);
//     connect_msg.seq = 0;
//     sendto_dbg(server_sock, (char *)&connect_msg, sizeof(connect_msg), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

//     printf("Sent CONNECT to server. Waiting for ACCEPT...\n");

//     FD_ZERO(&read_fds);
//     FD_SET(server_sock, &read_fds);
//     timeout.tv_sec = 2;
//     timeout.tv_usec = 0;

//     ret = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);
//     if (ret > 0 && FD_ISSET(server_sock, &read_fds)) {
//         struct ctrl_msg response;
//         ret = recvfrom(server_sock, &response, sizeof(response), 0, (struct sockaddr *)&server_addr, &server_addr_len);
//         if (ret > 0 && ntohl(response.type) == MSG_TYPE_ACCEPT) {
//             printf("Connection ACCEPTED.\n");
//         } else {
//             fprintf(stderr, "Connection REJECTED or timed out.\n");
//             exit(1);
//         }
//     } else {
//         fprintf(stderr, "No response from server.\n");
//         exit(1);
//     }

//     while(1) {
//         FD_ZERO(&read_fds);
//         FD_SET(server_sock, &read_fds);

//         timeout.tv_sec = 0;
//         timeout.tv_usec = 100000; // 100ms

//         ret = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);

//         if (ret > 0 && FD_ISSET(server_sock, &read_fds)) {
//             char recv_buf[MAX_MESS_LEN];
//             ret = recvfrom(server_sock, recv_buf, MAX_MESS_LEN, 0, NULL, NULL);
//             if (ret > 0) {
//                 struct ctrl_msg *header = (struct ctrl_msg *)recv_buf;
//                 if (ntohl(header->type) == MSG_TYPE_DATA) {
//                     struct stream_pkt *pkt = (struct stream_pkt *)(recv_buf + sizeof(struct ctrl_msg));
//                     pkt->seq = ntohl(pkt->seq);
//                     pkt->ts_sec = ntohl(pkt->ts_sec);
//                     pkt->ts_usec = ntohl(pkt->ts_usec);
//                     int32_t seq = ntohl(header->seq);
//                     int index = seq % RCV_BUF_SIZE;

//                     if (!recv_buffer[index].received) {
//                         packets_received++;
//                         recv_buffer[index].seq = seq;
//                         gettimeofday(&recv_buffer[index].recv_ts, NULL);
//                         memcpy(&recv_buffer[index].pkt, pkt, sizeof(struct stream_pkt));
//                         recv_buffer[index].received = 1;
//                         recv_buffer[index].delivered = 0;
//                         recv_buffer[index].nack_sent = 0; // Reset on receive

//                         if (!base_delta_set) {
//                             base_delta_sec = recv_buffer[index].recv_ts.tv_sec - pkt->ts_sec;
//                             base_delta_usec = recv_buffer[index].recv_ts.tv_usec - pkt->ts_usec;
//                             base_delta_set = 1;
//                             printf("Base delta calculated: %ld s, %ld us\n", base_delta_sec, base_delta_usec);
//                         }

//                         long send_sec = pkt->ts_sec;
//                         long send_usec = pkt->ts_usec;

//                         recv_buffer[index].delivery_ts.tv_sec = send_sec + base_delta_sec;
//                         recv_buffer[index].delivery_ts.tv_usec = send_usec + base_delta_usec + (Latency_Window * 1000);
//                         // Normalize timeval
//                         recv_buffer[index].delivery_ts.tv_sec += recv_buffer[index].delivery_ts.tv_usec / 1000000;
//                         recv_buffer[index].delivery_ts.tv_usec %= 1000000;
                        
//                         if (seq > highest_received_seq) {
//                             highest_received_seq = seq;
//                         }

//                         long delay = (recv_buffer[index].recv_ts.tv_sec - send_sec) * 1000000 + (recv_buffer[index].recv_ts.tv_usec - send_usec);
//                         total_delay += delay;
//                     }
//                 }
//             }
//         }

//         check_and_deliver_packets();
//         request_missing_packets();

//         static struct timeval last_stats_time = {0, 0};
//         struct timeval now;
//         gettimeofday(&now, NULL);
//         if (now.tv_sec - last_stats_time.tv_sec >= 5) {
//             print_stats();
//             last_stats_time = now;
//         }
//     }
//     return 0;
// }

// static void check_and_deliver_packets() {
//     struct timeval now;
//     gettimeofday(&now, NULL);

//     while (1) {
//         int index = lowest_undelivered_seq % RCV_BUF_SIZE;
//         if (recv_buffer[index].received && !recv_buffer[index].delivered) {
//             if (now.tv_sec > recv_buffer[index].delivery_ts.tv_sec ||
//                 (now.tv_sec == recv_buffer[index].delivery_ts.tv_sec && now.tv_usec >= recv_buffer[index].delivery_ts.tv_usec)) {
                
//                 sendto(app_sock, &recv_buffer[index].pkt, sizeof(struct stream_pkt), 0, (struct sockaddr *)&app_addr, sizeof(app_addr));
//                 recv_buffer[index].delivered = 1;
//                 lowest_undelivered_seq++;
//             } else {
//                 // Packet not ready for delivery
//                 break;
//             }
//         } else {
//             // Packet not yet received
//             // Check if we should consider it lost
//             long deadline_sec = recv_buffer[index].delivery_ts.tv_sec;
//             if (base_delta_set && (now.tv_sec > deadline_sec || (now.tv_sec == deadline_sec && now.tv_usec > recv_buffer[index].delivery_ts.tv_usec))) {
//                  if (recv_buffer[index].seq == lowest_undelivered_seq) { // Make sure we are not checking an old packet
//                     packets_missed++;
//                     lowest_undelivered_seq++;
//                  } else {
//                     break; // Not the one we are looking for
//                  }
//             } else {
//                 break; // Not yet time to consider it lost
//             }
//         }
//     }
// }

// static void request_missing_packets() {
//     for (int32_t seq = lowest_undelivered_seq; seq < highest_received_seq; seq++) {
//         int index = seq % RCV_BUF_SIZE;
//         if (!recv_buffer[index].received && !recv_buffer[index].nack_sent) {
//             struct ctrl_msg nack_msg;
//             nack_msg.type = htonl(MSG_TYPE_NACK);
//             nack_msg.seq = htonl(seq);
//             sendto_dbg(server_sock, (char *)&nack_msg, sizeof(nack_msg), 0, (struct sockaddr *)&server_addr, server_addr_len);
//             recv_buffer[index].nack_sent = 1; // Mark that we sent a NACK
//         }
//     }
// }

// static void print_stats() {
//     printf("\n--- Receiver Stats ---\n");
//     printf("Packets Received: %ld\n", packets_received);
//     printf("Packets Missed: %ld\n", packets_missed);
//     if (packets_received > 0) {
//         printf("Avg One-Way Delay: %.2f ms\n", (double)total_delay / packets_received / 1000.0);
//     }
//     printf("Lowest Undelivered Seq: %d\n", lowest_undelivered_seq);
//     printf("Highest Received Seq: %d\n", highest_received_seq);
//     printf("----------------------\n\n");
// }

// /* Read commandline arguments */
// static void Usage(int argc, char *argv[]) {

//     if (argc != 5) {
//         Print_help();
//     }

//     if (sscanf(argv[1], "%d", &Loss_rate) != 1) {
//         Print_help();
//     }

//     Hostname = strtok(argv[2], ":");
//     if (Hostname == NULL) {
//         printf("Error: no hostname provided\n");
//         Print_help();
//     }
//     Server_Port_Str = strtok(NULL, ":");
//     if (Server_Port_Str == NULL) {
//         printf("Error: no port provided\n");
//         Print_help();
//     }

//     App_Port_Str = argv[3];

//     if (sscanf(argv[4], "%d", &Latency_Window) != 1) {
//         Print_help();
//     }
// }

// static void Print_help(void) {
//     printf("Usage: rt_rcv <loss_rate_percent> <server_hostname>:<server_port> <app_port> <latency_window>\n");
//     exit(0);
// }



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "sendto_dbg.h"
#include "net_include.h"
#include "rt_protocol.h"

/* ------------------ CLI + usage ------------------ */
static void Usage(int argc, char *argv[]);
static void Print_help(void);

/* ------------------ Global state ------------------ */
static int server_sock = -1;
static int app_sock    = -1;

static struct sockaddr_in server_addr;           /* GLOBAL: used for NACKs */
static socklen_t          server_addr_len = sizeof(server_addr);

static struct sockaddr_in app_addr;

/* Receive ring */
static struct recv_buf_entry recv_buffer[RCV_BUF_SIZE];

/* NACK pacing (no header change: keep per-slot here) */
static struct timeval last_nack_ts[RCV_BUF_SIZE]; /* 0-initialized */
static const long NACK_COOLDOWN_US = 60000;       /* ~60ms ≈ 1.5×RTT (~40ms) */

/* baseDelta (sender->receiver clock offset + one-way) */
static long base_delta_sec = 0;
static long base_delta_usec = 0;
static int  base_delta_set = 0;

/* sequence tracking */
static int32_t lowest_undelivered_seq = 1;
static int32_t highest_received_seq   = 0;
static int32_t highest_delivered_seq  = 0;

/* stats */
static long   packets_received        = 0;   /* from sender */
static long   packets_delivered       = 0;   /* to local app (clean data) */
static long   total_delay_us          = 0;   /* sum(one-way) */
static long   min_delay_us            = -1;
static long   max_delay_us            = -1;

static struct timeval start_time = {0,0};    /* for rate calc */
static struct timeval last_stats = {0,0};

static void print_stats(void);
static void check_and_deliver_packets(void);
static void request_missing_packets(void);

/* ------------------ time helpers ------------------ */
static inline void tv_normalize(struct timeval *tv)
{
    while (tv->tv_usec >= 1000000) { tv->tv_sec++; tv->tv_usec -= 1000000; }
    while (tv->tv_usec < 0)        { tv->tv_sec--; tv->tv_usec += 1000000; }
}

static inline struct timeval tv_add(struct timeval a, struct timeval b)
{
    struct timeval r = { a.tv_sec + b.tv_sec, a.tv_usec + b.tv_usec };
    tv_normalize(&r);
    return r;
}

static inline struct timeval tv_sub(struct timeval a, struct timeval b)
{
    struct timeval r = { a.tv_sec - b.tv_sec, a.tv_usec - b.tv_usec };
    tv_normalize(&r);
    return r;
}

static inline int tv_cmp(struct timeval a, struct timeval b)
{
    if (a.tv_sec < b.tv_sec) return -1;
    if (a.tv_sec > b.tv_sec) return  1;
    if (a.tv_usec < b.tv_usec) return -1;
    if (a.tv_usec > b.tv_usec) return  1;
    return 0;
}

/* Build deliveryTime = sendTS + baseDelta + latencyWindow */
static inline struct timeval build_delivery_time(int32_t ts_sec, int32_t ts_usec, int latency_ms)
{
    struct timeval send_ts     = { ts_sec, ts_usec };
    struct timeval base_delta  = { base_delta_sec, base_delta_usec };
    struct timeval latency_tv  = { 0, latency_ms * 1000 };

    struct timeval r = tv_add(send_ts, base_delta);
    r = tv_add(r, latency_tv);
    return r;
}

/* ------------------ CLI vars ------------------ */
static int   Loss_rate;
static char *Hostname;
static char *Server_Port_Str;
static char *App_Port_Str;
static int   Latency_Window;

/* ------------------ Main ------------------ */
int main(int argc, char *argv[])
{
    struct hostent *host_ent;
    int server_port, app_port;

    fd_set read_fds;
    struct timeval timeout;

    Usage(argc, argv);
    sendto_dbg_init(Loss_rate);

    server_port = atoi(Server_Port_Str);
    app_port    = atoi(App_Port_Str);

    /* sockets */
    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) { perror("server_sock socket"); exit(1); }

    app_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (app_sock < 0) { perror("app_sock socket"); exit(1); }

    host_ent = gethostbyname(Hostname);
    if (!host_ent) {
        fprintf(stderr, "Could not resolve host: %s\n", Hostname);
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr, host_ent->h_addr_list[0], host_ent->h_length);
    server_addr.sin_port = htons(server_port);

    memset(&app_addr, 0, sizeof(app_addr));
    app_addr.sin_family      = AF_INET;
    app_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* deliver to localhost app */
    app_addr.sin_port        = htons(app_port);

    /* init buffer */
    for (int i = 0; i < RCV_BUF_SIZE; ++i) {
        recv_buffer[i].received   = 0;
        recv_buffer[i].delivered  = 0;
        recv_buffer[i].nack_sent  = 0;
        last_nack_ts[i].tv_sec    = 0;
        last_nack_ts[i].tv_usec   = 0;
    }

    gettimeofday(&start_time, NULL);
    last_stats = start_time;

    /* -------- Handshake: CONNECT -> wait ACCEPT ---------- */
    struct ctrl_msg connect_msg;
    connect_msg.type = htonl(MSG_TYPE_CONNECT);
    connect_msg.seq  = 0;
    sendto_dbg(server_sock, (char*)&connect_msg, sizeof(connect_msg), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));

    FD_ZERO(&read_fds);
    FD_SET(server_sock, &read_fds);
    timeout.tv_sec  = 2;
    timeout.tv_usec = 0;

    int ret = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);
    if (ret > 0 && FD_ISSET(server_sock, &read_fds)) {
        struct ctrl_msg response;
        socklen_t addrlen = server_addr_len;
        ret = recvfrom(server_sock, &response, sizeof(response), 0,
                       (struct sockaddr*)&server_addr, &addrlen);
        if (ret > 0 && ntohl(response.type) == MSG_TYPE_ACCEPT) {
            printf("Connection ACCEPTED to %s:%d\n", Hostname, server_port);
        } else {
            fprintf(stderr, "Connection REJECTED or unexpected response.\n");
            exit(1);
        }
    } else {
        fprintf(stderr, "No response (ACCEPT) from server.\n");
        exit(1);
    }

    /* --------------- Main event loop --------------- */
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        /* Dynamically compute timeout: next delivery due or a small cap (10ms) */
        struct timeval now;
        gettimeofday(&now, NULL);

        /* find next due delivery time starting from LUS */
        struct timeval next_due = { now.tv_sec + 3600, now.tv_usec }; /* far future */
        int have_due = 0;

        /* Only scan a reasonable window to keep this O(k), not O(N) */
        const int MAX_SCAN = 2048;
        int32_t scan_limit = lowest_undelivered_seq + MAX_SCAN;
        if (scan_limit > highest_received_seq + MAX_SCAN) {
            scan_limit = highest_received_seq + MAX_SCAN;
        }
        for (int32_t s = lowest_undelivered_seq; s <= scan_limit; ++s) {
            int idx = s % RCV_BUF_SIZE;
            if (recv_buffer[idx].received && !recv_buffer[idx].delivered) {
                struct timeval dt = recv_buffer[idx].delivery_ts;
                if (!have_due || tv_cmp(dt, next_due) < 0) {
                    next_due = dt;
                    have_due = 1;
                }
            }
        }

        if (have_due) {
            struct timeval delta = tv_sub(next_due, now);
            /* cap to 10ms max wait; if already due, zero */
            long cap_us = 10000;
            long d_us   = (delta.tv_sec < 0) ? 0 : delta.tv_sec * 1000000L + delta.tv_usec;
            if (d_us < 0) d_us = 0;
            if (d_us > cap_us) d_us = cap_us;
            timeout.tv_sec  = d_us / 1000000L;
            timeout.tv_usec = d_us % 1000000L;
        } else {
            /* no pending deliveries; wake frequently for NACK pacing and stats */
            timeout.tv_sec  = 0;
            timeout.tv_usec = 10000; /* 10ms */
        }

        ret = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);

        /* --- network receive --- */
        if (ret > 0 && FD_ISSET(server_sock, &read_fds)) {
            char recv_buf[MAX_MESS_LEN];
            int n = recvfrom(server_sock, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
            if (n > 0) {
                if (n < (int)sizeof(struct ctrl_msg)) {
                    /* ignore tiny frame */
                } else {
                    struct ctrl_msg *hdr = (struct ctrl_msg*)recv_buf;
                    int32_t mtype = ntohl(hdr->type);

                    if (mtype == MSG_TYPE_DATA) {
                        if (n < (int)(sizeof(struct ctrl_msg) + sizeof(struct stream_pkt))) {
                            /* malformed: ignore */
                        } else {
                            struct stream_pkt *pkt = (struct stream_pkt*)(recv_buf + sizeof(struct ctrl_msg));

                            /* convert inner pkt fields */
                            pkt->seq    = ntohl(pkt->seq);
                            pkt->ts_sec = ntohl(pkt->ts_sec);
                            pkt->ts_usec= ntohl(pkt->ts_usec);

                            int32_t seq = ntohl(hdr->seq);
                            int idx = seq % RCV_BUF_SIZE;

                            /* guard overwrite by comparing slot.seq when reusing slot */
                            if (!recv_buffer[idx].received || recv_buffer[idx].seq != seq) {
                                recv_buffer[idx].seq = seq;
                                recv_buffer[idx].received  = 1;
                                recv_buffer[idx].delivered = 0;
                                recv_buffer[idx].nack_sent = 0;     /* reset on success */
                                last_nack_ts[idx].tv_sec   = 0;
                                last_nack_ts[idx].tv_usec  = 0;
                                gettimeofday(&recv_buffer[idx].recv_ts, NULL);
                                memcpy(&recv_buffer[idx].pkt, pkt, sizeof(*pkt));
                                packets_received++;

                                if (!base_delta_set) {
                                    /* baseDelta = recvTime - sendTS */
                                    base_delta_sec = recv_buffer[idx].recv_ts.tv_sec  - pkt->ts_sec;
                                    base_delta_usec= recv_buffer[idx].recv_ts.tv_usec - pkt->ts_usec;
                                    struct timeval tmp = { base_delta_sec, base_delta_usec };
                                    tv_normalize(&tmp);
                                    base_delta_sec  = tmp.tv_sec;
                                    base_delta_usec = tmp.tv_usec;
                                    base_delta_set  = 1;
                                    printf("baseDelta set to %ld s, %ld us\n", base_delta_sec, base_delta_usec);
                                }

                                /* compute delivery time */
                                recv_buffer[idx].delivery_ts = build_delivery_time(pkt->ts_sec, pkt->ts_usec, Latency_Window);

                                if (seq > highest_received_seq) highest_received_seq = seq;

                                /* one-way delay stats */
                                long delay_us = (recv_buffer[idx].recv_ts.tv_sec - pkt->ts_sec) * 1000000L
                                              + (recv_buffer[idx].recv_ts.tv_usec - pkt->ts_usec);
                                total_delay_us += delay_us;
                                if (min_delay_us < 0 || delay_us < min_delay_us) min_delay_us = delay_us;
                                if (max_delay_us < 0 || delay_us > max_delay_us) max_delay_us = delay_us;
                            }
                        }
                    }
                }
            }
        }

        /* Deliver and request missing after each IO round */
        check_and_deliver_packets();
        request_missing_packets();

        /* periodic stats (every ~5s) */
        gettimeofday(&now, NULL);
        if (now.tv_sec - last_stats.tv_sec >= 5) {
            print_stats();
            last_stats = now;
        }
    }

    return 0;
}

/* ------------------ Delivery engine ------------------ */
/*
 * We deliver strictly increasing seq numbers starting from lowest_undelivered_seq,
 * but we DO NOT block forever on a missing head: if a later packet is already due,
 * we count the missing one as dropped (real-time deadline missed) and advance.
 * We never read delivery_ts for a missing slot.
 */
static void check_and_deliver_packets(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);

    while (1) {
        int idx = lowest_undelivered_seq % RCV_BUF_SIZE;

        if (recv_buffer[idx].received && !recv_buffer[idx].delivered) {
            /* if ready, deliver */
            if (tv_cmp(now, recv_buffer[idx].delivery_ts) >= 0) {
                /* deliver to local app (no loss emulation) */
                sendto(app_sock, &recv_buffer[idx].pkt, sizeof(struct stream_pkt), 0,
                       (struct sockaddr*)&app_addr, sizeof(app_addr));

                recv_buffer[idx].delivered = 1;
                highest_delivered_seq = lowest_undelivered_seq;
                lowest_undelivered_seq++;
                packets_delivered++;
                continue; /* try next */
            } else {
                /* not yet time for this packet */
                break;
            }
        } else {
            /* Missing head-of-line: see if a later received packet is due already */
            int advanced = 0;
            const int MAX_LOOKAHEAD = 1024;
            int32_t limit = lowest_undelivered_seq + MAX_LOOKAHEAD;
            if (limit > highest_received_seq) limit = highest_received_seq;

            for (int32_t s = lowest_undelivered_seq + 1; s <= limit; ++s) {
                int j = s % RCV_BUF_SIZE;
                if (recv_buffer[j].received && !recv_buffer[j].delivered) {
                    if (tv_cmp(now, recv_buffer[j].delivery_ts) >= 0) {
                        /* A later packet is already due -> this missing one is effectively dropped */
                        lowest_undelivered_seq++;
                        advanced = 1;
                        break;
                    }
                }
            }
            if (!advanced) break; /* nothing forces advancement yet */
        }
    }
}

/* ------------------ NACK pacing & retry ------------------ */
static void request_missing_packets(void)
{
    if (!base_delta_set) return; /* until first packet, we don't know timing */

    struct timeval now;
    gettimeofday(&now, NULL);

    for (int32_t seq = lowest_undelivered_seq; seq < highest_received_seq; ++seq) {
        int idx = seq % RCV_BUF_SIZE;

        if (!recv_buffer[idx].received) {
            /* cooldown check */
            struct timeval last = last_nack_ts[idx];
            long elapsed_us = 0;
            if (last.tv_sec != 0 || last.tv_usec != 0) {
                struct timeval diff = tv_sub(now, last);
                elapsed_us = diff.tv_sec * 1000000L + diff.tv_usec;
            } else {
                elapsed_us = NACK_COOLDOWN_US + 1; /* allow first NACK immediately */
            }

            if (elapsed_us >= NACK_COOLDOWN_US) {
                struct ctrl_msg nack_msg;
                nack_msg.type = htonl(MSG_TYPE_NACK);
                nack_msg.seq  = htonl(seq);

                sendto_dbg(server_sock, (char*)&nack_msg, sizeof(nack_msg), 0,
                           (struct sockaddr*)&server_addr, server_addr_len);

                recv_buffer[idx].nack_sent = 1;
                last_nack_ts[idx] = now; /* update cooldown */
            }
        }
    }
}

/* ------------------ Stats ------------------ */
static void print_stats(void)
{
    struct timeval now = {0,0};
    gettimeofday(&now, NULL);
    struct timeval run_dur = tv_sub(now, start_time);
    double secs = run_dur.tv_sec + run_dur.tv_usec / 1e6;

    /* Spec “drops” definition: highest_seq_seen - total_packets_received */
    long drops_spec = (highest_received_seq > 0) ? (highest_received_seq - packets_received) : 0;

    /* Compute experienced loss rate (%) */
    double experienced_loss_rate = 0.0;
    if (highest_received_seq > 0) {
        experienced_loss_rate = ((double)drops_spec / (double)highest_received_seq) * 100.0;
    }

    /* Clean data sizes (payload only) */
    const double BYTES_PER_PKT = (double)MAX_DATA_LEN;
    double clean_bytes = packets_delivered * BYTES_PER_PKT;
    double clean_mb    = clean_bytes / (1024.0 * 1024.0);

    double mbit_sent   = (clean_bytes * 8.0) / 1e6; /* megabits */
    double avg_mbps    = (secs > 0.0) ? (mbit_sent / secs) : 0.0;
    double avg_pps     = (secs > 0.0) ? (packets_delivered / secs) : 0.0;

    double avg_delay_ms = (packets_received > 0) ? (total_delay_us / 1000.0) / (double)packets_received : 0.0;
    double min_delay_ms = (min_delay_us >= 0) ? (min_delay_us / 1000.0) : 0.0;
    double max_delay_ms = (max_delay_us >= 0) ? (max_delay_us / 1000.0) : 0.0;

    printf("\n----- Receiver Report (rt_rcv) -----\n");
    printf(" Clean data delivered:  %.2f MB  (%ld packets)\n", clean_mb, packets_delivered);
    printf(" Avg rate (clean):      %.2f Mbit/s,  %.2f pkts/s\n", avg_mbps, avg_pps);
    printf(" Highest delivered seq: %d\n", highest_delivered_seq);
    printf(" Highest seen seq:      %d\n", highest_received_seq);
    printf(" Packets received:      %ld\n", packets_received);
    printf(" Spec loss (drops):     %ld  (= highest_seen - received)\n", drops_spec);
    printf(" Experienced loss rate: %.2f %%\n", experienced_loss_rate);
    printf(" One-way delay (ms):    avg=%.2f  min=%.2f  max=%.2f\n",
           avg_delay_ms, min_delay_ms, max_delay_ms);
    printf("------------------------------------\n");
}


/* ------------------ CLI ------------------ */
static void Usage(int argc, char *argv[])
{
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

    printf("Initialized:\n");
    printf("  Loss rate       = %d\n", Loss_rate);
    printf("  Server          = %s:%s\n", Hostname, Server_Port_Str);
    printf("  App port        = %s\n", App_Port_Str);
    printf("  Latency window  = %d ms\n", Latency_Window);
}

static void Print_help(void)
{
    printf("Usage: rt_rcv <loss_rate_percent> <server_hostname>:<server_port> <app_port> <latency_window>\n");
    exit(0);
}
