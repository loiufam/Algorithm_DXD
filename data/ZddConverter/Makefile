CC = g++

PRG = matrix_to_zdd

OPT = -std=c++17

OBJ = matrix_to_zdd.o

all: $(PRG)

$(PRG): $(OBJ)
	$(CC) $(OPT) $(OBJ) -o $(PRG)

matrix_to_zdd.o: matrix_to_zdd.cpp
	$(CC) $(OPT) -c matrix_to_zdd.cpp

clean:
	rm -f $(OBJ) 