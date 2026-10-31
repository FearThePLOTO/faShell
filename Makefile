CFLAGS = -Wall -Wextra

faShell: main.c
	$(CC) $(CFLAGS) -o faShell main.c

clean:
	rm -f faShell

.PHONY: clean