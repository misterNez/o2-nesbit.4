CC = gcc -g
PROGS = oss user
OBJ1 = oss.o
OBJ2 = user.o
INCLDIRS = timer.h message.h pct.h

all: $(PROGS)

oss: $(OBJ1) $(INCLDIRS)
	$(CC) -o $@ $^

user: $(OBJ2) $(INCLDIRS)
	$(CC) -o $@ $^

clean:
	rm -rf *.o *.log $(PROGS)
