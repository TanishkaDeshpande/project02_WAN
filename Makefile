CC=gcc

CFLAGS = -c -Wall -pedantic -g

all: udp_stream udp_stream_rcv rt_srv rt_rcv

udp_stream: udp_stream.o
	    $(CC) -o udp_stream udp_stream.o

udp_stream_rcv: udp_stream_rcv.o
	    $(CC) -o udp_stream_rcv udp_stream_rcv.o

rt_srv: rt_srv.o sendto_dbg.o
	    $(CC) -o rt_srv rt_srv.o sendto_dbg.o

rt_rcv: rt_rcv.o sendto_dbg.o
	    $(CC) -o rt_rcv rt_rcv.o sendto_dbg.o

clean:
	rm *.o

veryclean:
	rm udp_stream
	rm udp_stream_rcv
	rm rt_srv
	rm rt_rcv

%.o:    %.c
	$(CC) $(CFLAGS) $*.c


