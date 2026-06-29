# 🚀 APP LAUNCHER - Monitor 6502

**Lanzador de aplicaciones** para el **Monitor 6502** en Tang Nano 9K. Escanea la SD Card, lista los binarios disponibles y permite seleccionarlos y ejecutarlos via **UART** o **display TM1638** con teclado.

## 🎮 Controles

### UART (terminal serial)
| Tecla | Acción |
|-------|--------|
| `W` / `w` / `8` | Subir en la lista |
| `S` / `s` / `2` | Bajar en la lista |
| `ENTER` | Cargar y ejecutar selección |
| `Q` / `q` | Salir al monitor |

### TM1638 Display
| Tecla | Acción |
|-------|--------|
| **Key 1 (S1)** | Subir en la lista |
| **Key 2 (S2)** | Bajar en la lista |
| **Key 8 (S8)** | Cargar y ejecutar selección |
| **Key 7 (S7)** | Salir al monitor |

El display TM1638 muestra el **nombre del archivo** seleccionado (8 caracteres).

## ✨ Características

- ✅ Escanea automáticamente la SD Card al iniciar
- ✅ Lista hasta **60 aplicaciones** encontradas en la SD
- ✅ Navegación por **UART** y **TM1638** simultánea
- ✅ Los binarios se cargan en `$0800` y se ejecutan automáticamente
- ✅ **Sin límite de tamaño** para las apps (usa `rom_mfs_load_run()`)
- ✅ Post-ejecución: reescanea la SD automáticamente
- ✅ Display TM1638 se apaga al salir al monitor
- ✅ Optimizado: solo redibuja líneas cambiadas (ANSI escape codes)
- ✅ `romapi.h` sin modificar

## 📋 Flujo de Operación

```
Inicio → SD init → MFS mount → Leer FAT cache ($0264) → Menú interactivo
                                                              ↓
                                                      Navegar W/S o K1/K2
                                                              ↓
                                                      Enter o K8 → rom_mfs_load_run()
                                                              ↓
                                                      Ejecuta app en $0800
                                                              ↓
                                                      Si retorna → reescanea SD
```

## 📁 Estructura del Proyecto

```
LAUNCHER/
├── src/
│   ├── main.c          # ✅ Lanzador completo (~580 líneas)
│   └── startup.s       # Inicialización runtime C (CC65)
├── config/
│   └── programa.cfg    # Configuración del linker
├── include/
│   ├── romapi.h        # ROM API del Monitor 6502 (sin modificar)
│   └── tm1638.h        # Librería TM1638 v2.0
├── lib/tm1638/
│   └── tm1638.c        # Driver TM1638
├── build/              # Objetos (generados)
├── output/
│   └── LAUNCHER.bin    # ✅ Binario final (~9.9 KB)
├── makefile            # Compilación
└── README.md           # Este documento
```

## 🔧 Compilación

```bash
make        # Compilar el programa
make clean  # Limpiar archivos generados
make info   # Ver tamaño del binario
make map    # Ver mapa de memoria
```

Requiere **CC65** instalado en `D:\cc65` (ajustar en `makefile` si es necesario).

## 💾 Instalación en SD

1. Compilar con `make`
2. Copiar `output/LAUNCHER.bin` a la SD Card como `LAUNCHER`
3. En el monitor 6502:
   ```
   LOAD LAUNCHER 0800
   R 0800
   ```

## 🧠 Detalles Técnicos

### Lectura del Directorio MFS

La función `$BF12` (`mfs_list`) de la ROM **está rota** en esta versión del Monitor 6502. En lugar de usarla, el launcher:

1. Llama a `rom_mfs_mount()` que cachea el directorio en RAM en `$0264`
2. Lee las entradas directamente desde `$0264+`
3. Cada entrada ocupa **32 bytes** con el formato:

```
[0-11]   Nombre del archivo (12 bytes, null-terminated)
[12-13]  Cluster de inicio / reservado
[14-15]  Tamaño del archivo en bytes (little-endian)
[16-31]  Padding / reservado
```

### Carga y Ejecución

Usa `rom_mfs_load_run(name, 0x0800)` que carga el binario directamente en `$0800` y salta a él, **sin buffer intermedio**, sin límite de tamaño.

### Mapa de Memoria

| Rango | Uso |
|-------|-----|
| `$0800-$312E` | Código, datos y BSS del launcher (~12 KB) |
| `$3E00-$3FFF` | Stack CC65 (512 bytes) |
| `$BF00-$BF84` | ROM API (Jump Table) |
| `$C000-$C0FF` | Puertos de I/O |

## 🛠️ Hardware Requerido

- **Tang Nano 9K** con Monitor 6502 v2.2.0+ y ROM API
- **SD Card** para almacenar las aplicaciones
- **Display TM1638** (8 dígitos + 16 teclas) conectado al puerto `0xC000`
- **Cable UART** para terminal serial

## ⚠️ Notas

- La SD debe estar formateada con **MFS** (MicroFS), el sistema de archivos del Monitor 6502
- Los binarios deben ser ejecutables desde `$0800` (formato estándar del monitor)
- `romapi.h` **no ha sido modificado** — se usa tal cual
- Para agregar más librerías, ver `include/romapi.h` para la ROM API y `lib/tm1638/` para el driver TM1638
