all: build

.PHONY: clean

build: test_util.o so_stdio.o
	gcc -Wall -g $^ -o testing

# testuleanu.o: test_ferror_read_large.c
# 	gcc -Wall -g -c $^ -o testuleanu.o

test_util.o: _test/src/test_util.c
	gcc -Wall -g -c $^

# hooks.o: _test/src/hooks.c
# 	gcc -Wall -g -c $^

so_stdio.o: so_stdio.c
	gcc -Wall -g -c $^

clean:
	rm -rf *.o
	rm -rf testing
