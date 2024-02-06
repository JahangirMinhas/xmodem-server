PORT=57681
CFLAGS=-DPORT=\$(PORT) -g -Wall -Werror -fsanitize=address
FLAGS=-g -Wall -Werror -fsanitize=address
DEPENDENCIES= xmodemserver.h crc16.h

all: server client

server: crc16.o xmodemserver.o
	gcc ${CFLAGS} -o $@ $^

client: crc16.o client1.o
	gcc ${FLAGS} -o $@ $^

%.o : %.c ${DEPENDENCIES}
	gcc ${CFLAGS} -c $<

clean:
	rm -f *.o server client