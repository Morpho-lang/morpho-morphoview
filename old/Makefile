src = $(wildcard *.c) ../deps/glad/src/glad.c ../deps/morpho5/varray.c ../deps/morpho5/memory.c
obj = $(src:.c=.o)
PREFIX = /usr/local

LDFLAGS  = -lm -lcblas -llapack -lglfw -lfreetype -lczmq 
CFLAGS   = -std=c99 -O3 -I/usr/local/include/freetype2 -I. -I../morpho5/datastructures -I. -I../deps/morpho5 -I../deps/glad/include -I../deps/glad/include

morphoview: $(obj)
	$(CC) -o $@ $^ $(LDFLAGS) $(MODULES) $(CFLAGS)
 
.PHONY: clean
clean:
	rm -f $(obj) morphoview

.PHONY: install
install: morphoview
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $< $(DESTDIR)$(PREFIX)/bin/morphoview6
	make clean
