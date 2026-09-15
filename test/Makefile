CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O1 -I../logic
SRC     = test_logic.c $(wildcard ../logic/*.c)
BIN     = run.exe

.PHONY: test clean
test: $(BIN)
	./$(BIN)
$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)
clean:
	rm -f $(BIN)
