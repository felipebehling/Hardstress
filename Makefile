# Makefile para HardStress
# Uso:
#   make          -> build padrão no diretório raiz
#   make clean    -> limpar
#   make release  -> otimizado para release
#   make watch    -> build automático ao salvar arquivos (requer 'entr')

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
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

# Flags de compilação e linkagem por plataforma
ifeq ($(OS),Windows_NT)
    # Windows (MSYS2/MinGW)
    CFLAGS_COMMON = -Wall -std=gnu11 $(GTK_CFLAGS) -D_WIN32_DCOM -I$(SRC_DIR)
    # MODIFICADO: Adicionada a flag -mwindows para ocultar o console
    LDFLAGS = $(GTK_LIBS) -lpthread -lm -lpdh -lole32 -lwbemuuid -loleaut32 -mwindows -lhpdf
else
    # Linux/Outros
    CFLAGS_COMMON = -Wall -std=gnu11 $(GTK_CFLAGS) -I$(SRC_DIR)
    LDFLAGS = $(GTK_LIBS) -L/usr/lib/x86_64-linux-gnu -lpthread -lm -lhpdf
endif

# Flags específicas para cada tipo de build
CFLAGS_DEBUG = -O2 -g
CFLAGS_RELEASE = -O3 -march=native -DNDEBUG

# Define o CFLAGS padrão como debug
CFLAGS ?= $(CFLAGS_COMMON) $(CFLAGS_DEBUG)

# Definições para os testes
TEST_TARGET = test_runner
TEST_SRC_DIR = test
TEST_SRCS = $(wildcard $(TEST_SRC_DIR)/*.c)
# Fontes da aplicação necessários para os testes (excluindo main, ui, core)
APP_TEST_SRCS = $(SRC_DIR)/metrics.c $(SRC_DIR)/utils.c
TEST_OBJS = $(TEST_SRCS:.c=.o) $(APP_TEST_SRCS:.c=.o)

# Flags de compilação para testes (agora com GTK para resolver dependências de header)
ifeq ($(OS),Windows_NT)
    TEST_CFLAGS = -Wall -std=gnu11 $(GTK_CFLAGS) -I$(SRC_DIR) -D_WIN32_DCOM
    TEST_LDFLAGS = $(GTK_LIBS) -lpthread -lm
else
    TEST_CFLAGS = -Wall -std=gnu11 $(GTK_CFLAGS) -I$(SRC_DIR)
    TEST_LDFLAGS = $(GTK_LIBS) -lpthread -lm
endif

.PHONY: all clean release watch test

all: $(TARGET)$(TARGET_EXT)

$(TARGET)$(TARGET_EXT): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Alvo para construir e rodar os testes
test: $(TEST_TARGET)$(TARGET_EXT)
	@./$(TEST_TARGET)$(TARGET_EXT)

$(TEST_TARGET)$(TARGET_EXT): $(TEST_OBJS)
	$(CC) -o $@ $^ $(TEST_LDFLAGS)

$(TEST_SRC_DIR)/%.o: $(TEST_SRC_DIR)/%.c
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

clean:
	$(RM) $(TARGET)$(TARGET_EXT) $(SRC_DIR)/*.o
	$(RM) $(TEST_TARGET)$(TARGET_EXT) $(TEST_SRC_DIR)/*.o

release:
	$(MAKE) all CFLAGS="$(CFLAGS_COMMON) $(CFLAGS_RELEASE)"

# ADICIONADO: Novo alvo para compilação automática
# Uso: make watch
# Depende da ferramenta 'entr' (instale via 'pacman -S entr' no MSYS2 ou 'apt-get install entr' no Debian/Ubuntu)
watch:
	@echo "--> Observando arquivos .c e .h para recompilação automática..."
	find $(SRC_DIR) -name '*.c' -o -name '*.h' | entr -c -d -r make
