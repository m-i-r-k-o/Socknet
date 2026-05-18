# Percorsi di installazione
INCDIR = /usr/local/include/
LIBDIR = /usr/local/lib/

# Compilatore e flags
CC = clang
CFLAGS = -std=c99 -fPIC -shared -pedantic -Wno-format-pedantic
FILE = socknet

# Regola generica: build + install
all:
	$(CC) $(CFLAGS) $(FILE).c -o lib$(FILE).dylib
	sudo cp lib$(FILE).dylib $(LIBDIR)
	sudo cp $(FILE).h $(INCDIR)
	rm lib$(FILE).dylib
	export DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH

rm:
	clear
	clear
	sudo rm $(INCDIR)$(FILE).h
	sudo rm $(LIBDIR)lib$(FILE).dylib
