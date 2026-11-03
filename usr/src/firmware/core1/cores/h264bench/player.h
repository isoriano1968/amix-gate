/*
 * player.h -- core 1's player (player.c) and what its platform provides.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

/* the platform: arm_main.c on core 1, tests/zzav_host.c on the PC */
extern volatile uint8_t *pl_board;      /* the board memory's start */
extern uint64_t pl_tick_hz;
uint64_t pl_ticks(void);
void hb_service(void);                          /* arm_main.c: zzcrypto jobs, when the core has them */
void pl_load(uint64_t idle, uint32_t frames);    /* ticks with nothing to do, frames shown: for ztop */
uint32_t pl_audio_pos(void);            /* bytes the audio DMA is into the sound ring */

/* play what ZZAV_CMD_OPEN set up, until ZZAV_CMD_CLOSE */
int player_run(void);

#endif
