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
│   └── tm1638.h        # Compatibilidad: redirige a la librería externa
├── lib/                # (sin librerías copiadas; el TM1638 es externo)
├── build/              # Objetos (generados)
├── output/
│   └── LAUNCHER.bin    # ✅ Binario final (~9.9 KB)
├── makefile            # Compilación
└── README.md           # Este documento
```

## 📦 Librería TM1638 (referenciada, no copiada)

El driver TM1638 vive en su propio repositorio y **este proyecto solo lo
referencia**, sin copiar código:

```
D:\Proyectos\libs\tm1638-6502-cc65
├── src/tm1638.c        # Implementación
├── include/tm1638.h    # Header público
└── docs/, tests/, examples/
```

El `makefile` apunta ahí con `TM1638_DIR`, compila
`$(TM1638_DIR)\src\tm1638.c` y añade `$(TM1638_DIR)\include` a `-I`. La ruta
está en una sola variable, cámbiala ahí si mueves la librería.

**Ventajas:**

- Una sola copia del driver para todos los proyectos: se actualiza con
  `git pull` en el repositorio de la librería.
- El repositorio de la librería trae `docs/`, `examples/` y un banco de
  pruebas (`make test`, requiere `sim65`).
- `include/tm1638.h` de este proyecto queda como puente de compatibilidad:
  redirige a la librería. **No contiene declaraciones**, así que nunca se
  desincroniza del driver.

**Cómo se resuelve el `#include`.** El orden de los `-I` importa y está
deliberado: `$(TM1638_DIR)\include` va **antes** de `include`.

1. `-I src` → no está
2. `-I $(TM1638_DIR)\include` → encuentra el header real de la librería ✅

Por eso `include/tm1638.h` de este proyecto **nunca se compila**: solo existe
como puente para el código de la aplicación (`#include "tm1638.h"`), que así
no necesita saber dónde vive la librería. `tm1638.c` tampoco pasa por ningún
puente: se incluye a sí mismo con `"../include/tm1638.h"`.

⚠️ **No inviertas el orden de esos dos `-I`.** Si `include` fuera primero,
CC65 encontraría el puente, y su `#include "tm1638.h"` no continuaría al
siguiente directorio: las guardas se llaman distinto, así que el header real
nunca se incluiría y aparecería *Call to undeclared function
'tm1638_show_text'*. El puente está protegido con una guarda de nombre
distinto (`TM1638_BRIDGE_OMITIDO_AL_COMPILAR`) precisamente para eso: si algún
día se incluye, la guarda no colisiona con `TM1638_H` y el error es explícito
en vez de silencioso.

**Nota:** al apuntar a una ruta absoluta, el proyecto no compila en otra
máquina sin editar `TM1638_DIR`. Si necesitas portabilidad, la alternativa es
un submódulo de Git:

```bash
git submodule add <url-del-repo> lib/tm1638
git submodule update --init --recursive
```

y dejar `TM1638_DIR = lib\tm1638`.

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
- Para agregar más librerías, ver `include/romapi.h` para la ROM API
- El driver TM1638 **no se copia** en este proyecto: se referencia desde
  `D:\Proyectos\libs\tm1638-6502-cc65` (ver sección Librería TM1638)
