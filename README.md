# 🚀 APP LAUNCHER - Monitor 6502

**Lanzador de aplicaciones** para el **Monitor 6502** en Tang Nano 9K. Escanea la SD Card, lista los binarios disponibles y permite seleccionarlos y ejecutarlos. La salida va **simultáneamente** a la terminal serie (UART), al **display HDMI** (core de vídeo) y al **display TM1638**. La entrada acepta UART, joystick Atari y los botones del TM1638.

## 🎮 Controles

### UART (terminal serial)
| Tecla | Acción |
|-------|--------|
| `W` / `w` / `8` | Subir en la lista |
| `S` / `s` / `2` | Bajar en la lista |
| `ENTER` | Cargar y ejecutar selección |
| `Q` / `q` | Salir al monitor |

### Joystick Atari (DB9)
| Dirección / Botón | Acción |
|-------|--------|
| **Arriba** | Subir en la lista |
| **Abajo** | Bajar en la lista |
| **FIRE** | Cargar y ejecutar selección |
| **IZQUIERDA** | Encender/apagar el display del TM1638 |

El botón IZQUIERDA se añadió para **silenciar el ruido** que el módulo TM1638
inyecta en el audio: apagado, no circula corriente por los segmentos. No afecta
al resto de la entrada (UART y botones del TM1638 siguen funcionando).

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
- ✅ Lista hasta **16 aplicaciones** (límite del caché de directorio del MFS)
- ✅ Navegación por **UART**, **joystick** y **TM1638** simultánea
- ✅ Los binarios se cargan en `$0800` y se ejecutan automáticamente
- ✅ **Sin límite de tamaño** para las apps (usa `rom_mfs_load_run()`)
- ✅ Post-ejecución: reescanea la SD automáticamente
- ✅ Display TM1638 se apaga al salir al monitor
- ✅ HDMI muestra la app seleccionada (modo texto del core de vídeo)
- ✅ Display TM1638 apagable con el joystick (elimina ruido en el audio)
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
│   ├── main.c          # ✅ Lanzador completo
│   ├── joy.c / joy.h   # Driver del joystick Atari (Puerto 1, bits 3-7)
│   └── startup.s       # Inicialización runtime C (CC65)
├── config/
│   └── programa.cfg    # Configuración del linker
├── include/
│   ├── romapi.h        # ROM API del Monitor 6502 (sin modificar)
│   └── tm1638.h        # Compatibilidad: redirige a la librería externa
├── build/              # Objetos (generados)
├── output/
│   └── LAUNCHER.bin    # ✅ Binario final
├── makefile            # Compilación
└── README.md           # Este documento
```

No hay carpeta `lib/`: las dos librerías (TM1638 y core de vídeo) se
**referencian** desde sus repositorios, no se copian. Ver más abajo.

## 📦 Librerías (referenciadas, no copiadas)

Este proyecto **no copia código de librerías**. Ambas se referencian desde su
propio repositorio, que se actualiza con `git pull`:

```
D:\Proyectos\tm1638-6502-cc65            # driver del display y teclado
├── src/tm1638.c
├── include/tm1638.h
└── docs/, tests/, examples/

D:\Proyectos\juegos_6502\videocore-6502-cc65   # core de vídeo (modo texto)
├── src/video.h
├── output/vc.lib
└── docs/, examples/
```

El `makefile` apunta a ellas con `TM1638_DIR` y `VC_DIR`, y el orden de los
`-I` importa (ver comentario en el makefile). Cambiar esas variables si las
librerías se mueven.

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
| `$0800-$3B01` | Código, datos y BSS del launcher |
| `$3D00-$3DFF` | Stack CC65 (256 bytes) |
| `$BF00-$BF84` | ROM API (Jump Table) |
| `$C000-$C0FF` | Puertos de I/O |

> ⚠️ **Límite del monitor: hasta `$312E`.** El monitor reserva RAM por encima de
> esa dirección, así que un programa que crezca más allá la pisa. Este launcher
> **ya está por encima** pero funciona porque el monitor solo recarga el launcher
> desde la SD, que es como se usa normalmente. Si se carga por XMODEM a `$0800`
> puede reiniciarse. El README antiguo citaba `$312E` como fin del launcher
> (12 KB); con el joystick y el HDMI creció a ~12.8 KB.
>
> El `apps[16]` (240 bytes) vive en BSS y **no debe acercarse al stack**: si el
> código crece, el stack lo pisa y los nombres del listado se corrompen (se ve
> solo la primera letra del último archivo). Dejar al menos ~200 bytes de
> separación; hoy hay ~509.

### Colores del menú HDMI

El fondo de las celdas vacías es **transparente** (color 0), así que deja ver la
entrada **BG_COLOR** de la paleta. No se inicializa sola: hay que llamar
`vc_set_bgcolor(VC_BG_COLOR_DEFAULT)` al arrancar, o el fondo queda en lo que el
core tenga por defecto.

La fuente de texto del core **pinta siempre el color 3 de la paleta** (la
"tinta"), nunca un índice de color suelto. O sea: el color de la letra lo decide
la paleta de la celda. De ahí dos cosas:

- `PAL_NORMAL` = `VC_BGPAL_0` → letra blanca sobre fondo azul (preset)
- `PAL_SELECTED` = `VC_BGPAL_1`, **con colores redefinidos** al arrancar:
  color 1 (fondo) azul oscuro, color 3 (tinta) verde

```c
#define SEL_BG   VC_RGB444(0, 0, 5)    /* fondo: azul muy oscuro */
#define SEL_INK  VC_RGB444(0, 15, 0)   /* tinta: verde puro */
```

> ⚠️ **No usar `VC_BGPAL_2` para texto.** Su color 3 es verde, igual que su
> fondo: el texto sale verde sobre verde y se ve borroso. Se probó y se
descartó; las paletas preseleccionadas no garantizan contraste entre tinta y
> fondo.

`VC_RGB444(r, g, b)` usa un nibble por canal (0-15), no 0-255.

### Ruido en el audio

El módulo QYF-TM1638 inyecta ruido en el audio, y **escala con la cantidad de
segmentos encendidos**: más caracteres en el display, más ruido. El ruido viene
de la corriente de los segmentos, **no** de la velocidad de conmutación.

Medidas, y qué funcionó:

| Medida | Resultado |
|--------|-----------|
| Aislamiento de pines (el driver solo toca CLK/DIO/STB) | ✅ necesario |
| Subir `timing_delay` de 8 a 20 | ❌ mismo ruido, display más lento |
| **Apagar el display** (`tm1638_display_off()`) | ✅ **elimina el ruido** |

Por eso el botón **IZQUIERDA** del joystick alterna el display. La librería
expone `tm1638_display_off()` / `tm1638_display_on()`, que mandan el comando
`0x80` (Display OFF real; `set_brightness(0)` NO apaga, solo baja al mínimo).

### Joystick Atari (DB9)

En el Puerto 1, bits 3-7 (los 0-2 son del TM1638):

| Señal | Bit | Máscara |
|-------|-----|---------|
| right | 3 | `0x08` |
| left | 4 | `0x10` |
| down | 5 | `0x20` |
| up | 6 | `0x40` |
| fire | 7 | `0x80` |

Activo por nivel bajo (0 = pulsado), requiere pull-ups en el FPGA.

> ⚠️ **El registro `$C002` se reescribe en cada vuelta del bucle.** El TM1638
> también escribe ese registro para alternar el bit DIO, y si el FPGA no
> devuelve por lectura lo escrito (registro write-only), su read-modify-write
> pisa la configuración del joystick y deja de responder a los pocos segundos.
> Llamar `joy_init()` antes de cada `joy_read()` resuelve esto.

FIRE se lee **por nivel, sin detección de flanco**: `launch_app()` bloquea el
bucle durante la carga de SD, así que el estado previo queda desincronizado y
el flanco se pierde. Arriba/abajo sí usan flanco, para moverse de a un paso.

## 🛠️ Hardware Requerido

- **Tang Nano 9K** con Monitor 6502 v2.2.0+ y ROM API
- **SD Card** para almacenar las aplicaciones
- **Display TM1638** (8 dígitos + 16 teclas) conectado al puerto `0xC000`
- **Joystick Atari (DB9)** en el Puerto 1, bits 3-7
- **Cable UART** para terminal serial
- **Salida HDMI** (core de vídeo) para el menú

## ⚠️ Notas

- La SD debe estar formateada con **MFS** (MicroFS), el sistema de archivos del Monitor 6502
- Los binarios deben ser ejecutables desde `$0800` (formato estándar del monitor)
- `romapi.h` **no ha sido modificado** — se usa tal cual
- Máximo **16 aplicaciones**: es el límite del caché de directorio del MFS
- Para agregar más librerías, ver `include/romapi.h` para la ROM API
- El driver TM1638 **no se copia** en este proyecto: se referencia desde
  `D:\Proyectos\libs\tm1638-6502-cc65` (ver sección Librería TM1638)
