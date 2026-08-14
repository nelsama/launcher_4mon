# ============================================================================
# Makefile - App Launcher para Monitor 6502
# ============================================================================
# Uso:
#   make        - Compilar el programa
#   make clean  - Limpiar archivos generados
#   make info   - Ver tamaño del binario
#   make map    - Ver mapa de memoria
# ============================================================================

# Fuerza cmd.exe como shell en Windows (funciona desde PowerShell, CMD o Git Bash)
ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
endif

# Configuración CC65 - Ajustar ruta si es necesario
CC65_HOME = D:\cc65

# Herramientas
CC = cl65
CA65 = $(CC65_HOME)\bin\ca65.exe
LD = $(CC65_HOME)\bin\ld65.exe

# Directorios
SRC_DIR = src
CONFIG_DIR = config
BUILD_DIR = build
OUTPUT_DIR = output
LIB_DIR = lib
TM1638_DIR = $(LIB_DIR)\tm1638

# Configuración del linker
LD_CONFIG = $(CONFIG_DIR)\programa.cfg

# ============================================
# LIBRERÍAS (agregar más aquí)
# ============================================

# Nombre del programa
PROGRAM_NAME = LAUNCHER

# Archivos de salida
PROGRAM = $(OUTPUT_DIR)\$(PROGRAM_NAME).bin
MAP_FILE = $(OUTPUT_DIR)\$(PROGRAM_NAME).map

# Archivos fuente
C_SOURCES = $(SRC_DIR)\main.c $(TM1638_DIR)\tm1638.c
ASM_SOURCES = $(SRC_DIR)\startup.s

# Archivos objeto
C_OBJECTS = $(BUILD_DIR)\main.o $(BUILD_DIR)\tm1638.o
ASM_OBJECTS = $(BUILD_DIR)\startup.o

OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Flags del compilador C
CFLAGS = -t none -O --cpu 6502 -I $(SRC_DIR) -I include -I $(TM1638_DIR)

# Flags del ensamblador
ASFLAGS = -t none --cpu 6502

# Flags del linker
LDFLAGS = -C $(LD_CONFIG) -m $(MAP_FILE)

# ============================================================================
# REGLAS PRINCIPALES
# ============================================================================

all: dirs $(PROGRAM)
	@echo.
	@echo ========================================
	@echo Programa generado: $(PROGRAM)
	@for %%I in ($(PROGRAM)) do @echo Tamano: %%~zI bytes
	@echo ========================================
	@echo Para usar:
	@echo   1. Copiar a SD como $(PROGRAM_NAME)
	@echo   2. En el monitor:
	@echo      LOAD $(PROGRAM_NAME) 0800
	@echo      R 0800
	@echo ========================================

dirs:
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
	@if not exist "$(OUTPUT_DIR)" mkdir "$(OUTPUT_DIR)"

# Compilar C
$(BUILD_DIR)\main.o: $(SRC_DIR)\main.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)\tm1638.o: $(TM1638_DIR)\tm1638.c include\tm1638.h
	$(CC) -c $(CFLAGS) -o $@ $(TM1638_DIR)\tm1638.c

# Ensamblar
$(BUILD_DIR)\startup.o: $(ASM_SOURCES)
	$(CA65) $(ASFLAGS) -o $@ $<

# Linkar
$(PROGRAM): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) $(CC65_HOME)\lib\none.lib


# ============================================================================
# UTILIDADES
# ============================================================================

info:
	@echo ========================================
	@echo Informacion del programa
	@echo ========================================
	@if exist $(PROGRAM) (for %%I in ($(PROGRAM)) do @echo Tamano: %%~zI bytes) else @echo Error: Programa no compilado

map:
	@if exist $(MAP_FILE) (type $(MAP_FILE)) else @echo Error: Archivo de mapa no encontrado. Compilar primero.

clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	@if exist $(OUTPUT_DIR) rmdir /s /q $(OUTPUT_DIR)
	@echo Limpieza completa

# ============================================================================
# AYUDA
# ============================================================================

help:
	@echo Uso del makefile:
	@echo   make        - Compilar el programa
	@echo   make clean  - Limpiar archivos generados
	@echo   make info   - Ver informacion del binario
	@echo   make map    - Ver mapa de memoria

.PHONY: all dirs clean info map help
