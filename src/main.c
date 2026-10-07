/**
 * ============================================================================
 * APP LAUNCHER - Lanzador de aplicaciones para Monitor 6502
 * ============================================================================
 *
 * Lanza binarios desde SD Card con interfaz UART y TM1638.
 *
 * Controles UART:
 *   W/w            - Subir en la lista
 *   S/s            - Bajar en la lista
 *   ENTER (0x0D)   - Cargar y ejecutar seleccion
 *
 * Controles TM1638:
 *   Key 1 (S1)     - Subir en la lista
 *   Key 2 (S2)     - Bajar en la lista
 *   Key 8 (S8)     - Cargar y ejecutar seleccion
 *
 * ROM API utilizada:
 *   - rom_sd_init()       - Inicializar SD Card
 *   - rom_mfs_mount()     - Montar sistema de archivos
 *   - rom_mfs_list_via_zp - Listar archivos
 *   - rom_mfs_open()      - Abrir archivo
 *   - rom_mfs_get_size()  - Obtener tamaño
 *   - rom_mfs_read_via_zp - Leer datos
 *   - rom_mfs_close()     - Cerrar archivo
 *   - rom_uart_putc()     - Enviar caracteres UART
 *   - rom_uart_puts()     - Enviar strings UART
 *   - rom_uart_getc()     - Leer caracter UART
 *   - rom_uart_rx_ready() - Verificar datos UART
 *   - rom_delay_ms()      - Delays
 *
 * Librería externa:
 *   - TM1638 v2.0        - Display y teclado
 *
 * Mapa de memoria:
 *   $0800-$2CA9  - Código, datos y BSS del launcher (~9.3KB)
 *   $2D00-$3DFF  - Buffer de carga para apps (hasta 4.3KB)
 *   $3E00-$3FFF  - Stack del sistema (512 bytes)
 * ============================================================================
 */

#include <stdint.h>
#include "romapi.h"
#include "tm1638.h"
#include "joy.h"
#include "video.h"


/* ============================================================================
 * CONSTANTES
 * ============================================================================ */

/* Maximo de aplicaciones listables.
 *
 * apps[] ocupa MAX_APPS * sizeof(app_entry_t) = MAX_APPS * 15 bytes.
 * El MFS del monitor cachea el directorio en $0264 con FAT_ENTRIES = 16
 * entradas, asi que no tiene sentido reservar para mas.
 *
 * Con 16: 16 * 15 = 240 bytes, que caben en el stack de 256 del programa.
 * Con 60 (valor anterior) eran 900 bytes y desbordaba el stack en ~650,
 * corrompiendo codigo y datos (el sintoma del ": " convertido en "NI"). */
#define MAX_APPS            16      /* = FAT_ENTRIES: el MFS solo cachea 16 */
#define APP_NAME_LEN        13      /* name(12) + null + margen */
#define LOAD_ADDR           0x0800  /* Dirección de carga de los binarios */
/* El buffer temporal ya no es necesario: rom_mfs_load_run()
 * carga el archivo directamente en $0800 sobrescribiendonos. */

/* Separadores de 40 columnas: coinciden EXACTAMENTE con VC_SCREEN_COLS.
 * Un caracter de mas hace que la pantalla envuelva y deje una linea huerfana. */
#define SEP_EQ  "========================================"   /* 40 '=' */
#define SEP_DA  "----------------------------------------"   /* 40 '-' */

/* Brillo normal del display del TM1638 (0-7). El boton LEFT del joystick
 * lo alterna entre este valor y apagado total. */
#define TM1638_BRIGHTNESS   5

/* TM1638 key mapping */
#define TM_KEY_UP           1       /* S1 - Subir */
#define TM_KEY_DOWN         2       /* S2 - Bajar */
#define TM_KEY_SELECT       8       /* S8 - Seleccionar/Ejecutar */

/* UART key mapping */
#define UART_KEY_UP_W       'w'
#define UART_KEY_UP_W_CAPS  'W'
#define UART_KEY_DOWN_S     's'
#define UART_KEY_DOWN_S_CAPS 'S'
#define UART_KEY_UP_8       '8'
#define UART_KEY_DOWN_2     '2'
#define UART_KEY_SELECT     0x0D    /* Enter */
#define UART_KEY_QUIT       'q'
#define UART_KEY_QUIT_CAPS  'Q'

/* TM1638 key mapping */
#define TM_KEY_QUIT         7       /* S7 - Volver al monitor */


/* ============================================================================
 * ESTRUCTURA DE DATOS
 * ============================================================================ */

typedef struct {
    char     name[APP_NAME_LEN]; /* Nombre del archivo */
    uint16_t size;                 /* Tamaño en bytes */
} app_entry_t;

/* Lista de aplicaciones, en BSS y NO en el stack.
 *
 * El stack son 256 bytes (__STACKSIZE__ en programa.cfg). apps[16] ocupa 240,
 * dejando 6 bytes de margen: cualquier variable local nueva desborda y
 * corrompe los datos de encima.
 *
 * El sintoma es inconfundible: el listado repite bloques de lineas y el
 * joystick/boton dejan de responder, porque scroll_offset, joy_prev y demas
 * quedan pisados. */
static app_entry_t apps[MAX_APPS];


/* ============================================================================
 * FUNCIONES AUXILIARES UART
 * ============================================================================ */

void uart_print(const char *s) {
    while (*s) rom_uart_putc(*s++);
}

/* Numero de lineas escritas desde la ultima cabecera del listado.
 * Se usa para subir el cursor en el siguiente redibujado (la UART no tiene
 * direccionamiento de pantalla, asi que el listado se reescribe encima). */
static uint8_t uart_lines_written = 0;

/* La cabecera del listado se imprime UNA sola vez, no en cada redibujado.
 * Asi el bloque que se reposiciona no incluye lineas fijas y el conteo de
 * uart_lines_written no depende de cuantas lineas ocupe la cabecera. */
static uint8_t uart_header_done = 0;

/* Escribe una linea limpiandola antes (ESC[K) y terminando en CRLF.
 * El borrado evita restos de lineas mas largas escritas previamente. */
static void uart_put_line(const char *s) {
    rom_uart_putc(0x1B);
    rom_uart_putc('[');
    rom_uart_putc('K');
    uart_print(s);
    uart_print("\r\n");
}

void uart_print_num16(uint16_t n) {
    char buf[6];
    uint8_t i = 0;
    uint8_t j;
    char tmp;

    if (n == 0) {
        rom_uart_putc('0');
        return;
    }

    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }

    /* Invertir */
    for (j = 0; j < i / 2; j++) {
        tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }

    for (j = 0; j < i; j++) {
        rom_uart_putc(buf[j]);
    }
}


/* ============================================================================
 * SALIDA DE TEXTO (UART + PANTALLA HDMI)
 * ============================================================================
 * El core de video se usa directamente (vc_*), sin capa intermedia: casi todo
 * el acceso a la VRAM es una llamada directa a la libreria.
 *
 * Lo unico que necesita codigo propio es el DOBLE DESTINO (UART + pantalla) y
 * el relleno de celdas, porque vc_put_str no rellena y dejaría restos del
 * texto anterior al redibujar encima.
 */

/* Fila del cursor secuencial de la pantalla. console-like: avanza a medida que
 * se imprime, para los mensajes de arranque que van de arriba a abajo. */
static uint8_t scr_row = 0;

/**
 * @brief Escribe una cadena en la pantalla tratando '\n' como salto de linea.
 *
 * vc_put_str ya envuelve al llegar a la columna 40, pero no interpreta '\n'.
 * Devuelve la fila siguiente a la ultima escrita.
 */
static uint8_t screen_puts(uint8_t row, const char *s) {
    uint8_t col = 0;

    while (*s) {
        if (*s == '\n') {
            row++;
            if (row >= VC_SCREEN_ROWS) row = 0;
            col = 0;
        } else if (*s != '\r') {
            vc_put_cell(col, row, (uint8_t)*s);
            col++;
            if (col >= VC_SCREEN_COLS) {
                col = 0;
                row++;
                if (row >= VC_SCREEN_ROWS) row = 0;
            }
        }
        s++;
    }

    return row;
}

/**
 * @brief Imprime en los DOS destinos (UART y pantalla).
 *
 * Es el equivalente a console_print: existe porque los mensajes de flujo
 * (arranque, errores) deben verse tanto en el terminal serie como en HDMI, y
 * sin esta funcion cada mensaje habria que escribirlo dos veces.
 */
static void print_dual(const char *s) {
    const char *p;

    for (p = s; *p; p++) {
        rom_uart_putc(*p);
    }
    scr_row = screen_puts(scr_row, s);
}

/**
 * @brief Escribe texto en una fila, rellenando hasta 'width' columnas.
 *
 * Rellenar (en vez de solo escribir el texto) garantiza que al redibujar una
 * fila no queden restos del texto anterior mas largo.
 */
static void put_padded(uint8_t col, uint8_t row, const char *s,
                       uint8_t width, uint8_t paleta) {
    uint8_t i;

    for (i = 0; i < width; i++) {
        uint8_t c;

        if (*s != '\0') {
            c = (uint8_t)*s;
            s++;
        } else {
            c = VC_CHAR_SPACE;
        }

        vc_put_cell(col + i, row, c);
        vc_set_cell_attr(col + i, row, paleta, 0);
    }
}

/**
 * @brief Escribe un numero decimal alineado a la derecha en 'width' columnas.
 */
static void put_num(uint8_t col, uint8_t row, uint16_t n,
                    uint8_t width, uint8_t paleta) {
    char buf[6];
    uint8_t len = 0;
    uint8_t i;
    uint8_t pad;

    if (n == 0) {
        buf[len] = '0';
        len++;
    } else {
        while (n > 0 && len < 5) {
            buf[len] = (char)('0' + (n % 10));
            n /= 10;
            len++;
        }
    }

    pad = (width > len) ? (width - len) : 0;
    for (i = 0; i < pad; i++) {
        vc_put_cell(col + i, row, VC_CHAR_SPACE);
        vc_set_cell_attr(col + i, row, paleta, 0);
    }
    for (i = 0; i < len; i++) {
        vc_put_cell(col + pad + i, row, (uint8_t)buf[len - 1 - i]);
        vc_set_cell_attr(col + pad + i, row, paleta, 0);
    }
}


/* ============================================================================
 * FUNCIONES AUXILIARES TM1638
 * ============================================================================ */

/* Muestra un mensaje de error en TM1638 */
void tm1638_show_error(const char* msg) {
    tm1638_show_text(msg);
}

/* Muestra el nombre del archivo en el display (8 chars max) */
void tm1638_show_filename(const char* name) {
    char buf[9];
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (name[i] == 0) {
            for (; i < 8; i++) buf[i] = ' ';
            break;
        }
        buf[i] = name[i];
    }
    buf[8] = 0;
    tm1638_show_text(buf);
}


/* ============================================================================
 * ESCANEO DE APLICACIONES EN SD
 * ============================================================================
 *
 * mfs_mount() cachea el directorio MFS en $0264+ en RAM.
 * La funcion $BF12 (mfs_list) esta ROTA en la ROM, por eso NO la usamos.
 * En su lugar leemos directamente la cache del directorio.
 *
 * Cada entrada en $0264 ocupa 32 bytes:
 *   [0-11]:  nombre del archivo (12 bytes, null-terminated)
 *   [12-13]: cluster de inicio / reservado
 *   [14-15]: tamano del archivo en bytes (little-endian)
 *   [16-31]: padding / reservado
 * Maximo 16 entradas.
 * ============================================================================ */

#define FAT_ADDR     0x0264   /* Donde mfs_mount cachea el directorio */
#define FAT_ENTRIES  16       /* Maximo entradas en el directorio */
#define FAT_ENTRY_SZ 32       /* Cada entrada ocupa 32 bytes */

uint8_t scan_apps(app_entry_t* apps, uint8_t max_apps) {
    uint8_t count = 0;
    uint8_t result;
    uint8_t i;
    uint8_t j;
    uint8_t k;
    uint8_t* entry;
    uint16_t file_size;

    /* Sin mensajes de exito: solo se avisa si algo falla.
     * Cada linea de estado costaba ROM y solo interesa al depurar. */
    result = rom_sd_init();
    if (result != SD_OK) {
        uart_print("ERROR: SD init (");
        uart_print_num16(result);
        uart_print(")\r\n");
        return 0;
    }

    /* Montar MFS (cachea directorio en $0264) */
    result = rom_mfs_mount();
    if (result != MFS_OK) {
        uart_print("ERROR: MFS mount (");
        uart_print_num16(result);
        uart_print(")\r\n");
        return 0;
    }

    /* Leer entradas del directorio desde $0264 */
    for (i = 0; i < FAT_ENTRIES && count < max_apps; i++) {
        entry = (uint8_t*)(FAT_ADDR + i * FAT_ENTRY_SZ);

        if (entry[0] == 0) {
            continue;
        }

        /* Leer tamano REAL desde bytes 14-15 (little-endian) */
        file_size = entry[14] | ((uint16_t)entry[15] << 8);

        if (file_size == 0 || file_size > 30000) {
            continue;
        }

        /* Copiar nombre (bytes 0-11, max 12 chars, truncar en null) */
        k = 0;
        for (j = 0; j < 12 && entry[j] != 0; j++) {
            apps[count].name[k++] = entry[j];
        }
        apps[count].name[k] = '\0';
        apps[count].size = file_size;
        count++;
    }

    return count;
}


/* Helper: vuelve al monitor 6502 saltando a $8000 */
void quit_to_monitor(void) {
    tm1638_clear_display();
    uart_print("\r\nVolviendo al monitor...\r\n");
    rom_delay_ms(200);
    ((void (*)(void))0x8000)();
}


/* ============================================================================
 * MOSTRAR LISTA DE APLICACIONES
 * ============================================================================ */

/* Numero de lineas de una pagina del listado */
#define LIST_LINES  12  /* (legado UART; la pantalla usa su propio layout) */

/* Filas del listado que muestra la UART (ventana con scroll). Solo afecta a
 * la salida serie: en pantalla se muestra unicamente la app seleccionada. */
#define UART_LIST_ROWS  10

/* ============================================================================
 * LAYOUT DE LA PANTALLA (40x30)
 * ============================================================================
 * La pantalla muestra SOLO la app seleccionada, no el listado completo.
 * Motivo: el launcher debe caber por debajo de $312E, el techo de RAM que el
 * monitor deja libre. Dibujar las 26 filas del listado exigia un bucle, una
 * ventana con scroll y el borrado de filas sobrantes; mostrando solo la
 * seleccion nada de eso hace falta.
 *
 * El listado completo sigue disponible por UART, que es donde aporta.
 *
 * Reparto:
 *   fila  0        : titulo
 *   fila  1        : separador
 *   fila  6        : "SELECCIONADA:"
 *   fila  8        : nombre de la app (resaltado)
 *   fila 11        : separador
 *   fila 13        : "App N/Total"
 *   fila 15..16    : instrucciones (navegar / ejecutar)
 *   fila 22..      : zona de MENSAJES (cargando, errores)
 *
 * La zona de mensajes empieza por debajo de todo lo anterior para que
 * console_print (que avanza fila a fila) no pise el menu. launch_app vuelve
 * el cursor a SCR_MSG_ROW antes de escribir. */
#define SCR_TITLE_ROW    0
#define SCR_SEP1_ROW     1
#define SCR_LABEL_ROW    6
#define SCR_NAME_ROW     8
#define SCR_SEP2_ROW     11
#define SCR_INFO_ROW     13
#define SCR_HINT_ROW     15
#define SCR_MSG_ROW      22

/* Paletas del menu (del core de video) */
#define PAL_NORMAL       VC_BGPAL_0     /* texto normal */
#define PAL_SELECTED     VC_BGPAL_2     /* app seleccionada (resaltada) */

/**
 * @brief Muestra en pantalla SOLO la app seleccionada.
 *
 * Idempotente y sin estado: cada llamada deja la pantalla en el estado
 * correcto, asi que la navegacion no necesita recordar que cambio.
 */
static void screen_show_selected(app_entry_t* apps, uint8_t count,
                                 uint8_t selected) {
    put_padded(0, SCR_TITLE_ROW, "  APP LAUNCHER - Monitor 6502",
                       VC_SCREEN_COLS, PAL_NORMAL);
    put_padded(0, SCR_SEP1_ROW, SEP_DA, VC_SCREEN_COLS, PAL_NORMAL);

    put_padded(0, SCR_LABEL_ROW, "  SELECCIONADA:",
                       VC_SCREEN_COLS, PAL_NORMAL);

    /* Nombre con sangria, resaltado por paleta */
    put_padded(0, SCR_NAME_ROW, "   ", 3, PAL_SELECTED);
    put_padded(3, SCR_NAME_ROW, apps[selected].name,
                       VC_SCREEN_COLS - 3, PAL_SELECTED);

    put_padded(0, SCR_SEP2_ROW, SEP_DA, VC_SCREEN_COLS, PAL_NORMAL);

    /* Posicion en la lista */
    put_padded(0, SCR_INFO_ROW, " App", 4, PAL_NORMAL);
    put_num(4, SCR_INFO_ROW, selected + 1, 3, PAL_NORMAL);
    put_padded(7, SCR_INFO_ROW, "/", 1, PAL_NORMAL);
    put_num(8, SCR_INFO_ROW, count, 3, PAL_NORMAL);

    /* Instrucciones: navegar y ejecutar. Se separan en dos lineas para no
     * pasar de 40 columnas (41+ hace que la pantalla envuelva). */
    put_padded(0, SCR_HINT_ROW, "  W/S o K1/K2 = Navegar",
                       VC_SCREEN_COLS, PAL_NORMAL);
    put_padded(0, SCR_HINT_ROW + 1, "  ENTER o K8   = Cargar y ejecutar",
                       VC_SCREEN_COLS, PAL_NORMAL);

    /* Limpiar la zona de mensajes: al navegar debe quedar vacia.
     * Una sola llamada basta: con ancho VC_SCREEN_COLS limpia la fila entera. */
    put_padded(0, SCR_MSG_ROW, "", VC_SCREEN_COLS, PAL_NORMAL);
    put_padded(0, SCR_MSG_ROW + 1, "", VC_SCREEN_COLS, PAL_NORMAL);
}

/* Muestra el listado completo (con header y todo) */
void show_app_list_full(app_entry_t* apps, uint8_t count, uint8_t selected, uint8_t scroll_offset) {
    uint8_t i;
    uint8_t display_count;
    uint8_t idx;
    uint8_t len;

    /* La UART no tiene direccionamiento de pantalla: para redibujar hay que
     * reescribir el bloque encima del anterior.
     *
     * DIFERENCIA CLAVE con una primera version: aqui NO se reescribe la
     * cabecera. La cabecera se imprime UNA sola vez (ver uart_header_done) y
     * los redibujados posteriores suben el cursor solo sobre las filas de
     * apps, el separador y el pie. Si la cabecera entrara en el bloque
     * redibujado, cualquier error en el conteo de lineas desplazaria el
     * cursor y los listados se apilarian (nombres repetidos y basura). */
    if (!uart_header_done) {
        uart_put_line(SEP_EQ);
        uart_put_line("  APP LAUNCHER - Monitor 6502");
        uart_put_line(SEP_EQ);
        uart_put_line("  W/S=Navegar  ENTER=Ejec  Q=Salir");
        uart_put_line(SEP_DA);
        uart_header_done = 1;
    } else if (uart_lines_written > 0) {
        /* Volver al inicio de las filas de apps (el bloque anterior) */
        rom_uart_putc(0x1B);
        rom_uart_putc('[');
        uart_print_num16(uart_lines_written);
        rom_uart_putc('A');
    }

    display_count = (count - scroll_offset < UART_LIST_ROWS) ? (count - scroll_offset) : UART_LIST_ROWS;

    for (i = 0; i < display_count; i++) {
        idx = scroll_offset + i;

        rom_uart_putc(0x1B);
        rom_uart_putc('[');
        rom_uart_putc('K');   /* limpiar la linea */

        if (idx == selected) {
            uart_print(" > ");
        } else {
            uart_print("   ");
        }

        uart_print_num16(idx + 1);
        uart_print(": ");
        uart_print(apps[idx].name);

        len = 0;
        while (apps[idx].name[len] != '\0' && len < 12) len++;
        while (len < 12) {
            rom_uart_putc(' ');
            len++;
        }

        uart_print(" (");
        uart_print_num16(apps[idx].size);
        uart_print(" bytes)\r\n");
    }

    uart_put_line(SEP_DA);

    rom_uart_putc(0x1B);
    rom_uart_putc('[');
    rom_uart_putc('K');
    uart_print("  App ");
    uart_print_num16(selected + 1);
    uart_print("/");
    uart_print_num16(count);
    uart_print("\r\n");

    /* Lineas del bloque redibujable: display_count filas + 1 separador
     * + 1 pie. La cabecera NO cuenta: se imprime una sola vez. */
    uart_lines_written = display_count + 2;

    /* Pantalla HDMI: solo la app seleccionada (ver screen_show_selected).
     * El listado completo se queda en la UART: en pantalla manda el layout
     * de filas fijas y no cabe un listado con scroll. */
    screen_show_selected(apps, count, selected);
}


/* ============================================================================
 * CARGAR Y EJECUTAR APLICACIÓN
 * ============================================================================ */

/* Textos que se usan en los dos destinos (UART y pantalla). Declararlos una
 * vez evita que el compilador guarde dos copias casi identicas: la de UART
 * lleva '\r\n' y la de pantalla no, asi que no se deduplican solas. */
#define MSG_LOADING  "  CARGANDO Y EJECUTANDO"
#define MSG_ERR      "  ERROR: No se pudo cargar"

void launch_app(app_entry_t* app) {
    /* UART: aviso al flujo serie (una sola vez, no toca la pantalla). */
    uart_print("\r\n" SEP_EQ "\r\n");
    uart_print(MSG_LOADING "\r\n");
    uart_print(SEP_EQ "\r\n");
    uart_print("  Archivo: ");
    uart_print(app->name);
    uart_print("\r\n");
    uart_print("  Cargando en $0800...\r\n");

    /* Pantalla: la zona de mensajes, por debajo del menu.
     * Aqui SI se usa put_padded (posicion fija) y no console_print:
     * el cursor secuencial de pantalla no debe moverse por culpa de mensajes. */
    put_padded(0, SCR_MSG_ROW, SEP_EQ, VC_SCREEN_COLS, PAL_NORMAL);
    put_padded(0, SCR_MSG_ROW + 1, MSG_LOADING,
                       VC_SCREEN_COLS, PAL_NORMAL);
    put_padded(0, SCR_MSG_ROW + 2, "  Archivo:", 11, PAL_NORMAL);
    put_padded(11, SCR_MSG_ROW + 2, app->name, VC_SCREEN_COLS - 11,
                       PAL_NORMAL);

    /* Esperar a que la UART termine de transmitir antes de sobrescribirnos.
     * Las cadenas de arriba se encolan en el FIFO; sin esta espera, el
     * launcher se destruye a mitad de transmision y el log queda cortado. */
    while (!rom_uart_tx_ready()) { }
    rom_delay_ms(20);

    /* Apagar el display ANTES de cargar: una vez que rom_mfs_load_run
     * sobrescribe el launcher en $0800, ya no podemos usar el TM1638. */
    tm1638_clear_display();

    /* rom_mfs_load_run carga el archivo a la direccion y salta a ella.
     * ZP: $F4-$F5 = name ptr, $F6-$F7 = addr */
    rom_mfs_load_run(app->name, LOAD_ADDR);

    /* Si retorna, algo fallo */
    uart_print(MSG_ERR "\r\n");
    uart_print("  Presione ENTER para continuar\r\n");
    put_padded(0, SCR_MSG_ROW + 4, MSG_ERR,
                       VC_SCREEN_COLS, PAL_NORMAL);
    while (rom_uart_getc() != UART_KEY_SELECT);
}


/* ============================================================================
 * MAIN - PUNTO DE ENTRADA
 * ============================================================================ */

int main(void) {
    uint8_t app_count;
    uint8_t selected = 0;
    uint8_t scroll_offset = 0;
    uint8_t last_tm1638_key = 0;
    uint8_t key;
    char uart_char;
    uint8_t redraw_mode = 0;  /* 0=ninguno, distinto de 0 = redibujar */
    uint8_t needs_tm1638_update = 1;
    uint8_t last_selected = 0;
    uint8_t last_scroll = 0;
    uint8_t joy_prev = 0;   /* estado anterior del joystick, para el flanco */
    uint8_t display_on = 1; /* display del TM1638 encendido (LEFT lo alterna) */

    /* ============================================
     * INICIALIZACIÓN
     * ============================================ */

    /* Inicializar la salida HDMI (modo texto del core de video).
     * Espera a VIDEO_READY y limpia la pantalla. La UART ya la deja lista
     * el monitor, no necesita inicializacion aqui. */
    vc_wait_ready();
    vc_text_init();

    /* No hay banner ni mensajes de arranque: el menu HDMI y el listado UART
     * ya informan del estado, y cada linea costaba ROM. */

    /* Inicializar TM1638 */
    tm1638_init();
    tm1638_set_brightness(TM1638_BRIGHTNESS);
    tm1638_show_text(" LAUNCH ");

    /* Inicializar joystick (bits 3-7 del Puerto 1).
     * Preserva los bits 0-2 del TM1638 con read-modify-write. */
    joy_init();

    /* Escanear aplicaciones en SD */
    app_count = scan_apps(apps, MAX_APPS);

    if (app_count == 0) {
        /* Mensaje corto: se ve una vez si la SD falla. La lista detallada de
         * causas costaba ~150 bytes de ROM para algo que se lee una sola vez. */
        print_dual("\r\n" SEP_EQ "\r\n");
        print_dual("  NO HAY APLICACIONES\r\n");
        print_dual(SEP_EQ "\r\n");
        print_dual("  Revisar la SD (formato MFS).\r\n");
        print_dual("  RESET para reintentar.\r\n");

        tm1638_show_error("NO  FILE");

        while (1) {
            /* Bucle infinito - no hay apps */
            rom_delay_ms(1000);
        }
    }

    /* Ayuda: una sola linea. El detalle esta en el menu HDMI. */
    uart_print("\r\n  W/S o joystick: navegar.  ENTER/FIRE: cargar.  Q: salir\r\n");
    rom_delay_ms(500);

    /* El banner de arranque y el menu no pueden convivir: el banner usa el
     * cursor secuencial de console_print y el menu escribe en filas fijas.
     * Se limpia la pantalla para que el menu parta de un lienzo vacio. */
    vc_wait_vblank();
    vc_text_init();
    vc_wait_vblank_end();

    /* Dibujar listado inicial completo */
    last_scroll = scroll_offset;
    last_selected = selected;
    vc_wait_vblank();
    show_app_list_full(apps, app_count, selected, scroll_offset);
    vc_wait_vblank_end();
    needs_tm1638_update = 1;

    /* ============================================
     * BUCLE PRINCIPAL
     * ============================================ */

    while (1) {
        /* Redibujar si es necesario.
         * show_app_list_full es idempotente y repinta los dos destinos, asi
         * que ya no hay distincion entre redibujado completo y parcial.
         *
         * El dibujado va DENTRO de VBLANK: la VRAM solo se puede escribir
         * fuera de la zona visible. El menu hace ~500 escrituras por
         * redibujado; fuera de VBLANK el core puede leer la VRAM mientras
         * se escribe, y eso corrompe el estado del controlador. */
        if (redraw_mode) {
            vc_wait_vblank();
            show_app_list_full(apps, app_count, selected, scroll_offset);
            vc_wait_vblank_end();
            last_scroll = scroll_offset;
            last_selected = selected;
            redraw_mode = 0;
        }

        /* Actualizar TM1638 si es necesario */
        if (needs_tm1638_update) {
            tm1638_show_filename(apps[selected].name);
            needs_tm1638_update = 0;
        }

        /* ============================================
         * ENTRADA POR UART
         * ============================================ */

        if (rom_uart_rx_ready()) {
            uart_char = rom_uart_getc();

            /* Echo del carácter */
            /* (no hacemos echo para no ensuciar la pantalla) */

            if (uart_char == UART_KEY_UP_W || uart_char == UART_KEY_UP_W_CAPS || uart_char == UART_KEY_UP_8) {
                if (selected > 0) {
                    selected--;
                    /* Mantener la seleccion dentro de la ventana de 10 filas
                     * que muestra la UART. La pantalla HDMI solo muestra la
                     * app seleccionada, asi que no necesita ventana. */
                    if (selected < scroll_offset) {
                        scroll_offset = selected;
                    }
                    redraw_mode = 1;
                    needs_tm1638_update = 1;
                }
            }
            else if (uart_char == UART_KEY_DOWN_S || uart_char == UART_KEY_DOWN_S_CAPS || uart_char == UART_KEY_DOWN_2) {
                if (selected < app_count - 1) {
                    selected++;
                    if (selected >= scroll_offset + UART_LIST_ROWS) {
                        scroll_offset = selected - (UART_LIST_ROWS - 1);
                    }
                    redraw_mode = 1;
                    needs_tm1638_update = 1;
                }
            }
            else if (uart_char == UART_KEY_QUIT || uart_char == UART_KEY_QUIT_CAPS) {
                quit_to_monitor();
            }
            else if (uart_char == UART_KEY_SELECT) {
                /* Ejecutar aplicación seleccionada */
                uart_print("\r\n");
                launch_app(&apps[selected]);

                /* Si volvemos (el binario retornó o falló), continuar */
                uart_print("\r\n  Aplicacion finalizada. Reiniciando...\r\n\r\n");
                redraw_mode = 1;
                needs_tm1638_update = 1;

                /* Reescaneamos por si cambió algo */
                tm1638_show_text("RESCAN ");
                app_count = scan_apps(apps, MAX_APPS);
                if (app_count == 0) {
                    uart_print("  No hay apps disponibles\r\n");
                    tm1638_show_error("NO  FILE");
                    while (1) {
                        rom_delay_ms(1000);
                    }
                }
                if (selected >= app_count) {
                    selected = app_count - 1;
                }
                scroll_offset = 0;
            }
        }

        /* ============================================
         * ENTRADA POR JOYSTICK
         * ============================================
         * Se detecta el FLANCO de cada dirección (pulsación nueva), no el
         * nivel: mantener el joystick a un lado no debe recorrer la lista a
         * toda velocidad, sino moverse una posición por toque. */
        {
            uint8_t j;

            /* Reconfigurar el joystick en CADA vuelta.
             *
             * El TM1638 escribe el mismo registro $C002 (config del puerto) en
             * cada operacion para alternar el bit DIO. Si ese registro NO
             * devuelve por lectura lo que se escribio (habitual en FPGA con
             * registros de config write-only), el read-modify-write del driver
             * no preserva los bits 3-7 y deja la config del joystick corrupta:
             * el joystick funciona un momento y luego deja de responder.
             *
             * Reconfigurar aqui es barato (una escritura) y garantiza que los
             * bits 3-7 esten como entrada antes de leer. */
            joy_init();
            j = joy_read();

            /* LEFT: encender/apagar el display del TM1638 (toggle).
             *
             * Apagarlo elimina el ruido que el modulo inyecta en el audio:
             * con el display realmente OFF no circula corriente por los
             * segmentos ni se conmutan los pines del display.
             *
             * No afecta al resto: la lectura del teclado del TM1638 y los
             * botones K1/K2/K8/K7 siguen funcionando igual. */
            if ((j & JOY_A_LEFT) && !(joy_prev & JOY_A_LEFT)) {
                if (display_on) {
                    tm1638_display_off();
                    display_on = 0;
                } else {
                    tm1638_display_on();
                    display_on = 1;
                    needs_tm1638_update = 1;   /* repinta la seleccion actual */
                }
            }

            if ((j & JOY_A_UP) && !(joy_prev & JOY_A_UP)) {
                if (selected > 0) {
                    selected--;
                    if (selected < scroll_offset) {
                        scroll_offset = selected;
                    }
                    redraw_mode = 1;
                    needs_tm1638_update = 1;
                }
            }

            if ((j & JOY_A_DOWN) && !(joy_prev & JOY_A_DOWN)) {
                if (selected < app_count - 1) {
                    selected++;
                    if (selected >= scroll_offset + UART_LIST_ROWS) {
                        scroll_offset = selected - (UART_LIST_ROWS - 1);
                    }
                    redraw_mode = 1;
                    needs_tm1638_update = 1;
                }
            }

            /* Fire: cargar y ejecutar la app seleccionada.
             *
             * Sin deteccion de flanco, a diferencia de UP/DOWN: el manual del
             * joystick usa el nivel directamente (if (j & JOY_A_FIRE)), y
             * ademas launch_app() puede tardar (carga de SD), durante lo cual
             * el boton ya se solto. Con flanco, el estado previo quedaba
             * desincronizado tras una carga lenta. */
            if (j & JOY_A_FIRE) {
                uart_print("\r\n");
                launch_app(&apps[selected]);

                /* Si volvemos (el binario retornó o falló), continuar */
                uart_print("\r\n  Aplicacion finalizada. Reiniciando...\r\n\r\n");
                redraw_mode = 1;
                needs_tm1638_update = 1;

                tm1638_show_text("RESCAN ");
                app_count = scan_apps(apps, MAX_APPS);
                if (app_count == 0) {
                    uart_print("  No hay apps disponibles\r\n");
                    tm1638_show_error("NO  FILE");
                    while (1) {
                        rom_delay_ms(1000);
                    }
                }
                if (selected >= app_count) {
                    selected = app_count - 1;
                }
                scroll_offset = 0;
            }

            joy_prev = j;
        }

        /* ============================================
         * ENTRADA POR TM1638
         * ============================================ */

        key = tm1638_get_key_pressed();

        if (key > 0 && key != last_tm1638_key) {
            last_tm1638_key = key;

            if (key == TM_KEY_UP) {
                if (selected > 0) {
                    selected--;
                    if (selected < scroll_offset) {
                        scroll_offset = selected;
                        redraw_mode = 1;
                    } else {
                        redraw_mode = 2;
                    }
                    needs_tm1638_update = 1;
                }
            }
            else if (key == TM_KEY_DOWN) {
                if (selected < app_count - 1) {
                    selected++;
                    if (selected >= scroll_offset + UART_LIST_ROWS) {
                        scroll_offset = selected - (UART_LIST_ROWS - 1);
                    }
                    redraw_mode = 1;
                    needs_tm1638_update = 1;
                }
            }
            else if (key == TM_KEY_SELECT) {
                tm1638_show_text(" LOADING");
                launch_app(&apps[selected]);

                /* Si volvemos */
                tm1638_show_text("RESCAN ");
                app_count = scan_apps(apps, MAX_APPS);
                if (app_count == 0) {
                    tm1638_show_error("NO  FILE");
                    while (1) {
                        rom_delay_ms(1000);
                    }
                }
                if (selected >= app_count) {
                    selected = app_count - 1;
                }
                scroll_offset = 0;
                redraw_mode = 1;
                needs_tm1638_update = 1;
            }
            else if (key == TM_KEY_QUIT) {
                quit_to_monitor();
            }
        }
        else if (key == 0) {
            last_tm1638_key = 0;
        }

        /* Pequeña pausa para no saturar */
        rom_delay_ms(50);
    }

    /* return 0; -- No llegamos aquí */
    return 0;
}
