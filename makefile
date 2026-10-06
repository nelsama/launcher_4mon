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

# Libreria TM1638: repositorio externo referenciado, NO copiado.
# Comparte el codigo con otros proyectos; se actualiza con 'git pull'
# en D:\Proyectos\libs\tm1638-6502-cc65.
# Ajustar aqui si la libreria cambia de ubicacion.
TM1638_DIR = D:\Proyectos\libs\tm1638-6502-cc65

# Libreria del Core de Video (modo texto en HDMI). Tambien referenciada.
# Solo se enlazan los objetos que el launcher usa: no hay que sumar vc.lib
# completa (la demo que ejercita todo pesa ~5KB, esto pesa menos).
VC_DIR = D:\Proyectos\juegos_6502\videocore-6502-cc65

# Configuración del linker
LD_CONFIG = $(CONFIG_DIR)\programa.cfg

# ============================================
# LIBRERÍAS (agregar más aquí)
# ============================================

# Parametro de timing del driver TM1638: pausa entre flancos de CLK/DIO/STB.
# Con 0 (valor por defecto de la libreria) la conmutacion es muy rapida y puede
# inyectar ruido en senales que compartan el byte del puerto. La libreria lo
# protege con #ifndef para que cada proyecto lo sobrescriba sin editarla.
# Ver docs/PIN_ISOLATION_AND_NOISE.md en la libreria.
TM1638_TIMING_DELAY = 8

# Nombre del programa
PROGRAM_NAME = LAUNCHER

# Archivos de salida
PROGRAM = $(OUTPUT_DIR)\$(PROGRAM_NAME).bin
MAP_FILE = $(OUTPUT_DIR)\$(PROGRAM_NAME).map

# Archivos fuente
C_SOURCES = $(SRC_DIR)\main.c $(SRC_DIR)\console.c $(TM1638_DIR)\src\tm1638.c
ASM_SOURCES = $(SRC_DIR)\startup.s

# Archivos objeto
C_OBJECTS = $(BUILD_DIR)\main.o $(BUILD_DIR)\console.o $(BUILD_DIR)\tm1638.o
ASM_OBJECTS = $(BUILD_DIR)\startup.o

OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Flags del compilador C
# El orden de -I importa: $(TM1638_DIR)\include va ANTES de include para que
# "tm1638.h" resuelva al header real de la libreria. El include\tm1638.h del
# proyecto es solo un puente para el codigo de la aplicacion (y se omite al
# compilar). Si se invirtiera el orden, CC65 encuentra el puente primero y
# falla: las guardas !="headers de distinto nombre" cortan la busqueda y el
# header real nunca se incluye.
#
# -O  = optimiza por VELOCIDAD (por defecto).
#       Se probo -Os (por tamano) y no cambio nada: el driver TM1638 genera el
#       mismo codigo y las librerias externas ya vienen precompiladas con -O.
#       Ver commit para el detalle si alguna vez hace falta mas espacio.
CFLAGS = -t none -O --cpu 6502 -I $(SRC_DIR) -I $(TM1638_DIR)\include -I $(VC_DIR)\src -I include -Dtiming_delay=$(TM1638_TIMING_DELAY)

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
# main.c incluye "tm1638.h", que se resuelve al header de la libreria externa
# (ver orden de -I). Por eso main.o depende tambien de ese header: sin esta
# dependencia, un 'git pull' en la libreria no recompilaria main.o y quedaria
# con declaraciones viejas.
$(BUILD_DIR)\main.o: $(SRC_DIR)\main.c $(TM1638_DIR)\include\tm1638.h
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)\console.o: $(SRC_DIR)\console.c $(SRC_DIR)\console.h $(VC_DIR)\src\video.h include\romapi.h
	$(CC) -c $(CFLAGS) -o $@ $(SRC_DIR)\console.c

$(BUILD_DIR)\tm1638.o: $(TM1638_DIR)\src\tm1638.c $(TM1638_DIR)\include\tm1638.h
	$(CC) -c $(CFLAGS) -o $@ $(TM1638_DIR)\src\tm1638.c

# Ensamblar
$(BUILD_DIR)\startup.o: $(ASM_SOURCES)
	$(CA65) $(ASFLAGS) -o $@ $<

# Linkar
$(PROGRAM): $(OBJECTS) $(VC_DIR)\output\vc.lib
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) $(VC_DIR)\output\vc.lib $(CC65_HOME)\lib\none.lib


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
