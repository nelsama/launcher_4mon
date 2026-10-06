/**
 * @file tm1638.h
 * @brief Compatibilidad: redirige a la librería TM1638 externa.
 *
 * La librería NO está copiada en este proyecto: vive en
 * D:\Proyectos\libs\tm1638-6502-cc65 y se referencia desde el makefile
 * (TM1638_DIR).
 *
 * Este archivo existe solo para que el código de la aplicación pueda seguir
 * usando #include "tm1638.h" sin saber dónde está la librería. No contiene
 * declaraciones: el header real lo aporta -I $(TM1638_DIR)\include.
 *
 * Al compilar, este puente se omite: CC65 resuelve "tm1638.h" primero en
 * -I $(TM1638_DIR)\include, que precede a -I include. Ver makefile.
 */
#ifndef TM1638_BRIDGE_OMITIDO_AL_COMPILAR
#define TM1638_BRIDGE_OMITIDO_AL_COMPILAR

#include "tm1638.h"

#endif /* TM1638_BRIDGE_OMITIDO_AL_COMPILAR */
