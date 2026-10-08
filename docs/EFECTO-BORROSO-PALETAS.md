# Efecto "borroso": paletas de bajo contraste en el Core de Vídeo

**Documento técnico** — hallazgo durante el desarrollo del *App Launcher*
(Monitor 6502 / Tang Nano 9K) y su posible uso como efecto gráfico.

---

## 1. Resumen

En el Core de Vídeo, **la fuente de texto pinta siempre el color 3 de la
paleta de la celda**. Si ese color 3 es parecido al color de fondo, el texto no
se ve mal por un bug: se ve **borroso, translúcido o fantasmal**. Es un efecto
de **bajo contraste**, no un error de render.

Ese efecto, buscado a propósito, sirve para:

- **Suavizado / antialiasing simulado** en texto o en bordes de sprite
- **Fundidos (fade in/out)** cambiando una entrada de paleta
- **Translucidez** de sprites y sombras
- **Letras "fantasma"** o texto deshabilitado en menús

---

## 2. El hallazgo

Apareció al resaltar el nombre del archivo seleccionado en el menú del
launcher. Se usó `VC_BGPAL_2` para el texto resaltado, cuyo preset es:

```
VC_BGPAL_2     preset: verde / verde osc / verde
```

El texto se veía **borroso**. Se sospechó (mal) de:

- La función que escribía las celdas (dos escrituras separadas: carácter y
  atributo)
- La sincronización con VBLANK

**Ninguna era la causa.** Se comprobó que `vc_put_str_pal()` de la propia
librería hace exactamente las mismas dos escrituras, así que el patrón de
escritura era correcto.

La causa real: **la tinta (color 3) y el fondo de esa paleta eran ambos
verdes.** Verde sobre verde.

La prueba que lo confirmó: poner el resaltado en la **misma paleta** del texto
normal. El nombre se vio **nítido** (blanco sobre azul). Luego, redefinir el
color 3 a verde y el fondo a azul oscuro dio un **verde nítido**: mismo color
de letra, ahora con contraste.

---

## 3. Por qué pasa (el mecanismo)

### El color de la letra no es configurable por carácter

De `src/video.h`:

```
VC_COLOR3             3     /* color que usa la fuente de texto */
```

> La fuente pinta **SIEMPRE el color 3** (VC_COLOR3), así que el color de la
> letra lo elige la **PALETA de la celda**, no un índice de color.

O sea: **no se elige el color de la letra**. Se elige la paleta de la celda, y
esa paleta decide de qué color sale la letra (siempre su color 3).

### Consecuencia

```
paleta de la celda
  ├── color 0 → siempre transparente (se ve BG_COLOR)
  ├── color 1 → fondo (según el patrón del tile)
  ├── color 2 → fondo alternativo
  └── color 3 → TINTA: el color de la letra
```

Si `color 1` (fondo) y `color 3` (tinta) son **tonos cercanos**, la letra se
distingue poco del fondo. Eso es todo.

### Los presets no garantizan contraste

| Paleta | color 1 | color 2 | **color 3 (tinta)** | ¿Sirve para texto? |
|---|---|---|---|---|
| `VC_BGPAL_0` | azul | cian | **blanco** | ✅ contraste |
| `VC_BGPAL_1` | marrón | gris | **blanco** | ✅ contraste |
| `VC_BGPAL_2` | verde | verde oscuro | **verde** | ❌ **verde sobre verde** |
| `VC_BGPAL_3` | gris | marrón | **verde** | ⚠️ depende del fondo usado |

Ninguno de los presets fue diseñado pensando en "texto legible sobre su propio
fondo": son paletas para **tiles de fondo** (terreno, vegetación, cielo). El
color 3 quedó como tinta por convención, no por diseño de contraste.

---

## 4. Cómo se controla a propósito

### Paletas programables

De `src/video.h`:

> Por defecto valen lo de arriba, pero el CPU puede **REESCRIBIR** cualquier
> entrada en cualquier momento (RGB444, con auto-incremento). Útil para
> **fundidos, parpadeos, paletas por nivel, o tu propia paleta**.

```c
/* Fija UNA entrada: (paleta, color, RGB444) */
void vc_pal_set_bg(uint8_t paleta, uint8_t color, uint16_t rgb444);

/* Carga los 4 colores de una paleta de golpe */
void vc_pal_load_bg(uint8_t paleta, const uint16_t *colores4);
```

⚠️ `VC_RGB444(r, g, b)` toma **un nibble por canal (0-15)**, no 0-255.

### Ejemplo real (el launcher)

```c
#define PAL_SELECTED     VC_BGPAL_1
#define SEL_BG   VC_RGB444(0, 0, 5)     /* fondo: azul muy oscuro */
#define SEL_INK  VC_RGB444(0, 15, 0)    /* tinta: verde puro */

vc_pal_set_bg(PAL_SELECTED, 1, SEL_BG);
vc_pal_set_bg(PAL_SELECTED, 3, SEL_INK);
```

Resultado: **verde nítido sobre azul oscuro**. Se conserva el color deseado y
se gana el contraste que faltaba.

---

## 5. El efecto como recurso gráfico

### 5.1 ¿Aplica a sprites? — Sí, con matices

Los sprites usan **su propio banco de paletas** (`VC_SPPAL_0..3`, entradas
16-31) pero el mecanismo es el mismo:

```
Paletas de SPRITE: color 0 = transparente
  0: — / piel #FF8800 / marrón #884400 / negro #000000
  1: — / azul #0000FF / cian  #00FFFF / blanco #FFFFFF
  2: — / magenta #FF00FF / rojo #FF0000 / blanco #FFFFFF
  3: — / verde #00FF00 / naranja #FF8800 / blanco #FFFFFF
```

La **diferencia clave** es el **color 0**:

| | Fondo (tiles) | Sprite |
|---|---|---|
| color 0 | **transparente** (se ve BG_COLOR) | **transparente** |
| color 1, 2, 3 | colores del tile | colores del sprite |

O sea: en un sprite, **los colores 1, 2 y 3 se pintan**. Si hacés que el
**color 1 (el relleno típico) se acerque al fondo** sobre el que se dibuja el
sprite, el sprite se ve **translúcido**.

**Es controlable por banco de paleta** (`VC_SPPAL_0..3`), y cada sprite elige
su banco en el campo `flags`. Pero atención a la limitación:

> Una paleta de sprite es **global al banco**, no por sprite. Si reprogramás
> `VC_SPPAL_1`, **cambian de golpe todos los sprites que usen esa paleta.**

Para tener un sprite translúcido y otro normal del mismo tipo, hay que
**reservar bancos distintos** para cada variante (hay 4 bancos de sprite).

### 5.2 Técnicas concretas

**Fundido a negro / desde negro (fade)**
Reprogramar los colores hacia `0x000` progresivamente. No hay que redibujar
nada: es solo escribir la paleta. En el launcher se usó `vc_pal_set_bg`; para
sprites sería `vc_pal_set_spr`.

**Translucidez (sombra, agua, cristal)**
Hacer que el color de relleno del sprite se acerque al color de fondo. Sirve
para sombras proyectadas: un sprite negro con sus colores bajados de
intensidad se ve como sombra.

**Antialiasing simulado en texto**
Poner el color 3 de la paleta en un tono intermedio entre el fondo y la tinta
deseada. El texto pierde nitidez pero "pesa" menos visualmente. Útil para texto
**deshabilitado** (opción no seleccionable en un menú).

**Parpadeo / énfasis**
Alternar el color 3 entre tinta normal y tinta baja. Es un parpadeo por paleta,
más barato que redibujar: **unos pocos bytes escritos** contra reescribir todo
el tilemap.

**Indicador de estado sin cambiar el dibujo**
Mismo patrón de tile, distinta paleta: un enemigo "congelado" (tinte azul), un
personaje envenenado (tinte verde), un objeto seleccionado. **Cero cambios en
el tilemap o los patrones**, solo la paleta.

### 5.3 Consideraciones

- **Es gratis en CPU.** Cambiar una paleta son unos pocos bytes a los registros
  `$D813-$D815`; redibujar tiles cuesta mucho más.
- **Es reversible.** Guardá los 4 colores originales y restaurálos al terminar
  el efecto.
- **Aplica a toda la paleta, no a una celda.** Para efectos localizados hay que
  **reservar un banco de paleta** para ese uso. Hay 4 de fondo y 4 de sprite.
- **Cuidado con BG_COLOR.** El color de fondo global es la **entrada 15**, que
  **comparte** con el color 3 de la paleta 3: cambiar uno cambia el otro (ver
  manual del core).

---

## 6. Regla práctica

> **Antes de usar una paleta para texto, verificá que su color 3 contraste con
> el fondo sobre el que se va a dibujar.** Los presets del core no lo
> garantizan: son paletas de tiles, no de tipografía.

Y al revés:

> **Si querés un efecto de bajo contraste, bajá el contraste entre el color 3
> (tinta) y el color 1 (fondo) de la paleta de esa celda.** No hay que tocar el
> dibujo.

---

## 7. Referencias

- `src/video.h` del repositorio `videocore-6502-cc65`: secciones 4 (Paletas),
  5 (BG_COLOR), 6 (Atributo del fondo), 7 (Flags de sprite)
- `examples/palette/main.c`: demo de paletas programables
- `07-MANUAL-PROGRAMACION.md`: manual del core (§4.2 paletas de sprite,
  §4.3 BG_COLOR, §4.4 reprogramación)
- Origen de este hallazgo: resaltado del nombre seleccionado en el
  **App Launcher** (`launcher_4mon`)
