EXEC = im-httpd
CFLAGS = --ansi --pedantic -Wall -O2 -g3
OBJ = main.o sv_core.o conn.o

$(EXEC): $(OBJ)
	$(CC) -o $(EXEC) $(OBJ)

clean:
	rm -f $(OBJ) $(EXEC)

