CFLAGS=-std=c++11 -I /usr/local
CC=g++


TARGETS=sender receiver
UTILS=util.o 

all: $(TARGETS)

$(UTILS): %.o: %.cpp %.hpp
	$(CC) $(CFLAGS) -c $<

%.o: %.cpp util.hpp
	$(CC) $(CFLAGS)  -c $< 


$(TARGETS): %:%.o util.o
	$(CC) $(CFLAGS) $^ -o $@

run_sender:
	./sender

run_reciever:
	./receiver

.PHONY:	clean

clean:
	rm *.o $(HELPERS) $(TARGETS) 
