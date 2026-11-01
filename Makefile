CFLAGS = -Wall -Wextra
LDLIBS = -lreadline

faShell: main.c
	$(CC) $(CFLAGS) -o faShell main.c $(LDLIBS)

clean:
	rm -f faShell

.PHONY: clean