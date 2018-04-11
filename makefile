CC = gcc
FILES = server.c
OUT_EXE = server
FILES1 = client.c
OUT_EXE1 = client
build: $(FILES)
	$(CC) -o $(OUT_EXE) $(FILES)
	$(CC) -o $(OUT_EXE1) $(FILES1)
clean:
	-rm -f *.o core
rebuild: clean build
