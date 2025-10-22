#ifndef RT_PROTOCOL_H
#define RT_PROTOCOL_H

#include <sys/time.h>
#include "udp_stream_private.h"

/* Message Types */
#define MSG_TYPE_CONNECT 1
#define MSG_TYPE_ACCEPT  2
#define MSG_TYPE_REJECT  3
#define MSG_TYPE_DATA    4
#define MSG_TYPE_NACK    5
#define MSG_TYPE_ACK     6 // Reserved

/* Constants */
#define MAX_DATA_LEN 1300
#define MAX_BUFFER_PACKETS 20000
#define RCV_BUF_SIZE 20000
#define MAX_LATENCY_WINDOW 1000 // ms

/* Control Message Structure */
struct ctrl_msg {
    int32_t type; // message type
    int32_t seq;  // packet sequence number
};

/* Sender Buffer Entry */
struct send_buf_entry {
    int32_t seq;
    struct timeval send_ts;
    struct stream_pkt pkt;
    int valid;
    int retransmitted;
};

/* Receiver Buffer Entry */
struct recv_buf_entry {
    int32_t seq;
    struct timeval recv_ts;
    struct timeval delivery_ts;
    struct stream_pkt pkt;
    int received;
    int delivered;
    int nack_sent; // Flag to prevent excessive NACKs
};

#endif // RT_PROTOCOL_H