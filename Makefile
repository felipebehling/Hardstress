# Makefile para HardStress
# Uso:
#   make          -> build padrão
#   make clean    -> limpar
#   make release  -> otimizado para release

TARGET = HardStress
SRC = hardstress.c

PKG_CONFIG ?= pkg-config
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

CFLAGS = -O2 -Wall -std=gnu11 $(GTK_CFLAGS)
LDFLAGS = $(GTK_LIBS) -lpthread -lm

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o

release: clean
	$(CC) -O3 -march=native -DNDEBUG -Wall -std=gnu11 $(GTK_CFLAGS) -o $(TARGET) $(SRC) $(GTK_LIBS) -lpthread -lm
