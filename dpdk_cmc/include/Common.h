#pragma once
#include "Port.h"

static volatile bool force_quit = false;

// MMMS shutdown phase flag. Set by the SIGINT handler on the first Ctrl+C to
// halt normal TX while keeping RX alive long enough to drain the MMMS file
// transfer. A real (non-static) symbol so tx_worker and main loop see the
// same store.
extern volatile bool stop_normal_tx;

#define MAX_SOCKET 8
#define MAX_LCORE_PER_SOCKET 32

// Declare variables as extern (declarations only)
extern struct ports_config ports_config;
extern uint16_t socket_to_lcore[MAX_SOCKET][MAX_LCORE_PER_SOCKET];
extern uint16_t unused_socket_to_lcore[MAX_SOCKET][MAX_LCORE_PER_SOCKET];

// Number of TX/RX cores per port - can be overridden from Makefile
// Number of queues = Number of cores
#ifndef NUM_TX_CORES
#define NUM_TX_CORES 2
#endif

#ifndef NUM_RX_CORES
#define NUM_RX_CORES 2
#endif
