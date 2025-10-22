#ifndef CS2520_EX2_UDP_STREAM
#define CS2520_EX2_UDP_STREAM

#define TARGET_RATE 8 /* rate to send at, in Mbps */
#define MAX_DATA_LEN 1300
#define REPORT_SEC 5 /* print status every REPORT_SEC seconds */

struct stream_pkt {
    int32_t seq;
    int32_t ts_sec;
    int32_t ts_usec;
    char data[MAX_DATA_LEN];
};

#endif
