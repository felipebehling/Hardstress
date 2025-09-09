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

# Define CFLAGS e LDFLAGS com base no SO
ifeq ($(OS),Windows_NT)
    # Windows (MSYS2/MinGW)
    # Adicionado -lole32, -lwbemuuid para WMI e -lpdh para PDH
    CFLAGS = -O2 -Wall -std=gnu11 $(GTK_CFLAGS) -D_WIN32_DCOM
    LDFLAGS = $(GTK_LIBS) -lpthread -lm -lpdh -lole32 -lwbemuuid
else
    # Linux/Outros
    CFLAGS = -O2 -Wall -std=gnu11 $(GTK_CFLAGS)
    LDFLAGS = $(GTK_LIBS) -lpthread -lm
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) $(TARGET) *.o

release: clean
ifeq ($(OS),Windows_NT)
	$(CC) -O3 -march=native -DNDEBUG -Wall -std=gnu11 $(GTK_CFLAGS) -D_WIN32_DCOM -o $(TARGET) $(SRC) $(LDFLAGS)
else
	$(CC) -O3 -march=native -DNDEBUG -Wall -std=gnu11 $(GTK_CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)
endif
