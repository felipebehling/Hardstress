# Makefile para HardStress
# Uso:
#   make          -> build padrão no diretório raiz
#   make clean    -> limpar
#   make release  -> otimizado para release

# Detecta a plataforma para adicionar a extensão correta ao executável
TARGET = HardStress
ifeq ($(OS),Windows_NT)
    TARGET_EXT = .exe
else
    TARGET_EXT =
endif

# Localização dos fontes
SRC_DIR = src
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:.c=.o)

# Ferramentas e libs
PKG_CONFIG ?= pkg-config
CXX = g++ # <--- ADICIONADO
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

# Flags de compilação e linkagem por plataforma
ifeq ($(OS),Windows_NT)
    # Windows (MSYS2/MinGW)
    CFLAGS_COMMON = -Wall -std=gnu11 $(GTK_CFLAGS) -D_WIN32_DCOM -I$(SRC_DIR)
    LDFLAGS = $(GTK_LIBS) -lpthread -lm -lpdh -lole32 -lwbemuuid
else
    # Linux/Outros
    CFLAGS_COMMON = -Wall -std=gnu11 $(GTK_CFLAGS) -I$(SRC_DIR)
    LDFLAGS = $(GTK_LIBS) -lpthread -lm
endif

# Flags específicas para cada tipo de build
CFLAGS_DEBUG = -O2 -g
CFLAGS_RELEASE = -O3 -march=native -DNDEBUG

# Define o CFLAGS padrão como debug
CFLAGS ?= $(CFLAGS_COMMON) $(CFLAGS_DEBUG)

.PHONY: all clean release

all: $(TARGET)$(TARGET_EXT)

$(TARGET)$(TARGET_EXT): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) # <--- ALTERADO DE $(CC) para $(CXX)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CXX) $(CFLAGS) -c -o $@ $< # <--- ALTERADO DE $(CC) para $(CXX)

clean:
	$(RM) $(TARGET)$(TARGET_EXT) $(SRC_DIR)/*.o

release:
	$(MAKE) all CFLAGS="$(CFLAGS_COMMON) $(CFLAGS_RELEASE)"
