default: mines
clean:
	rm -f mines mines.o

mines: mines.o
mines.o: mines.c

