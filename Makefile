# Makefile para HardStress
# Uso:
#   make          -> build padrão no diretório raiz
#   make clean    -> limpar
#   make release  -> otimizado para release
#   make watch    -> build automático ao salvar arquivos (requer 'entr')

# --- Configurações Gerais ---
TARGET = HardStress
SRC_DIR = src
TEST_SRC_DIR = test

# Detecta a plataforma para adicionar a extensão correta ao executável
ifeq ($(OS),Windows_NT)
    TARGET_EXT = .exe
else
    TARGET_EXT =
endif

# --- Build Principal da Aplicação ---
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:.c=.o)

PKG_CONFIG ?= pkg-config
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk+-3.0)
HARU_CFLAGS := $(shell $(PKG_CONFIG) --cflags libharu)
HARU_LIBS   := $(shell $(PKG_CONFIG) --libs libharu)


# Flags de compilação e linkagem por plataforma
ifeq ($(OS),Windows_NT)
    # Windows (MSYS2/MinGW)
    CFLAGS_COMMON = -Wall -std=gnu11 $(GTK_CFLAGS) $(HARU_CFLAGS) -D_WIN32 -D_WIN32_DCOM -I$(SRC_DIR)
    LDFLAGS = $(GTK_LIBS) $(HARU_LIBS) -lpthread -lm -lpdh -lole32 -lwbemuuid -loleaut32 -mwindows -lhpdf
else
    # Linux/Outros
    CFLAGS_COMMON = -Wall -std=gnu11 $(GTK_CFLAGS) $(HARU_CFLAGS) -I$(SRC_DIR)
    LDFLAGS = $(GTK_LIBS) $(HARU_LIBS) -lpthread -lm -lhpdf
endif

CFLAGS_DEBUG = -O2 -g
CFLAGS_RELEASE = -O3 -march=native -DNDEBUG
CFLAGS ?= $(CFLAGS_COMMON) $(CFLAGS_DEBUG)

# --- Build dos Testes ---
TEST_TARGET = test_runner
# Fontes da aplicação necessários para os testes
APP_TEST_SRCS = $(SRC_DIR)/metrics.c $(SRC_DIR)/utils.c
# Fontes dos testes
TEST_SRCS = $(wildcard $(TEST_SRC_DIR)/*.c)
# Objs dos testes (com sufixo .test.o para evitar conflitos)
TEST_OBJS = $(TEST_SRCS:.c=.o) $(APP_TEST_SRCS:.c=.test.o)

# Flags de compilação para testes
ifeq ($(OS),Windows_NT)
    TEST_CFLAGS = -Wall -std=gnu11 $(GTK_CFLAGS) -I$(SRC_DIR) -D_WIN32 -D_WIN32_DCOM -DTESTING_BUILD
    TEST_LDFLAGS = $(GTK_LIBS) -lpthread -lm -lpdh -lole32 -lwbemuuid -loleaut32
else
    # Linux/Outros
    TEST_CFLAGS = -Wall -std=gnu11 $(GTK_CFLAGS) -I$(SRC_DIR) -DTESTING_BUILD
    TEST_LDFLAGS = $(GTK_LIBS) -lpthread -lm
endif

# --- Alvos do Makefile ---

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

# Regra para compilar fontes de teste (e.g., test/test_main.c -> test/test_main.o)
$(TEST_SRC_DIR)/%.o: $(TEST_SRC_DIR)/%.c
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

# Regra para compilar fontes da aplicação para os testes (e.g., src/metrics.c -> src/metrics.test.o)
$(SRC_DIR)/%.test.o: $(SRC_DIR)/%.c
	$(CC) $(TEST_CFLAGS) -c -o $@ $<

clean:
	$(RM) $(TARGET)$(TARGET_EXT) $(SRC_DIR)/*.o
	$(RM) $(TEST_TARGET)$(TARGET_EXT) $(TEST_SRC_DIR)/*.o $(SRC_DIR)/*.test.o


release:
	$(MAKE) all CFLAGS="$(CFLAGS_COMMON) $(CFLAGS_RELEASE)"

watch:
	@echo "--> Observando arquivos .c e .h para recompilação automática..."
	find $(SRC_DIR) $(TEST_SRC_DIR) -name '*.c' -o -name '*.h' | entr -c -d -r make