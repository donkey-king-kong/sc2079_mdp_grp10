CC = gcc
CFLAGS = -Wall -g
LIBS = -lpthread -lcurl
SRC = multithread_communication.c json_parser.c rpi_hal.c
OBJ = $(SRC:.c=.o)

.PHONY: ctrl_center and_center st_center test_center clean

ctrl_center: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o ctrl_center $(LIBS)

and_center: $(SRC)
	$(CC) $(CFLAGS) -DANDROID_SIM=1 $(SRC) -o and_center $(LIBS)

st_center: $(SRC)
	$(CC) $(CFLAGS) -DSTM32_SIM=1 $(SRC) -o st_center $(LIBS)

test_center: $(SRC)
	$(CC) $(CFLAGS) -DSTM32_SIM=1 -DANDROID_SIM=1 -DRPI_TESTING $(SRC) -o test_center $(LIBS)

clean:
	rm -f ctrl_center and_center st_center test_center *.o
