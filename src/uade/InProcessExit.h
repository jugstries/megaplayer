/*
 * Incluso a forza in tutti i sorgenti C di UADE quando uadecore gira in un
 * thread del player (vedi InProcess.cpp). L'emulatore chiama exit() per
 * molti errori, anche solo quando libuade chiude il canale: nel processo del
 * player terminerebbe tutto. Qui exit() termina solo il thread di uadecore.
 */
#ifndef MP_UADE_IN_PROCESS_EXIT_H
#define MP_UADE_IN_PROCESS_EXIT_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" [[noreturn]]
#else
_Noreturn
#endif
void uade_inprocess_exit(int status);

#define exit(status) uade_inprocess_exit(status)

#endif
