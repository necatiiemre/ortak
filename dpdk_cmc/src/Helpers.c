#include "Helpers.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <rte_ethdev.h>
#include <rte_atomic.h>

#include "Config.h"
#include "Packet.h"       // for PACKET_SIZE (byte approximation for per-CMC stats)
#include "TxRxManager.h"  // for rx_stats_per_port
#include "AteMode.h"

// Daemon mode flag - when true, ANSI escape codes are disabled
bool g_daemon_mode = false;

void helper_set_daemon_mode(bool enabled) {
    g_daemon_mode = enabled;
}

// Helper functions
static inline double to_gbps(uint64_t bytes) {
    return (bytes * 8.0) / 1e9;
}

#if STATS_MODE_CMC
static void reset_cmc_prev_bytes(void);
#endif

void helper_reset_stats(const struct ports_config *ports_config,
                        uint64_t prev_tx_bytes[], uint64_t prev_rx_bytes[])
{
    // Reset HW statistics and zero out prev_* counters
    for (uint16_t i = 0; i < ports_config->nb_ports; i++) {
        uint16_t port_id = ports_config->ports[i].port_id;
        rte_eth_stats_reset(port_id);
        prev_tx_bytes[port_id] = 0;
        prev_rx_bytes[port_id] = 0;
    }

    // Reset RX validation statistics (PRBS) + per-VL sequence trackers
    init_rx_stats();

    // Reset TX VL-ID commit counters so shared-queue CMC RX
    // (Σ tx_vl_sequence) does not carry warm-up residue into the test window.
    reset_tx_vl_sequences();

#if STATS_MODE_CMC
    init_cmc_stats();
    reset_cmc_prev_bytes();
#endif
}

#if STATS_MODE_CMC
// ==========================================
// CMC PORT-BASED STATISTICS - 2-TABLE DISPLAY (Net A / Net B)
// ==========================================
// CMC has exactly two flows. Each gets its own table; layout & columns are
// the same as the legacy CMC tables (Good / Bad / SplitMix64 Fail / CRC32
// Fail / Loss / Bit Error / BER) so the operator sees a familiar shape.
// CMC TX (CMC→Server) = Server RX = HW q_ipackets[queue]
// CMC RX (Server→CMC) = Server TX = HW q_opackets[queue]
// PRBS = cmc_stats[cmc_port] (from RX worker)

// Per-CMC-port prev bytes for delta calculation
static uint64_t cmc_prev_tx_bytes[CMC_PORT_COUNT];
static uint64_t cmc_prev_rx_bytes[CMC_PORT_COUNT];

// Zero the per-CMC prev-byte baselines. Invoked from helper_reset_stats at
// warm-up → test transition so the first test-window Gbps delta is computed
// against zero, not the warm-up byte total.
static void reset_cmc_prev_bytes(void)
{
    for (uint16_t i = 0; i < CMC_PORT_COUNT; i++) {
        cmc_prev_tx_bytes[i] = 0;
        cmc_prev_rx_bytes[i] = 0;
    }
}

// Count CMC flows that share the same server RX (queue) — used to decide
// whether the HW per-queue counter is dedicated to this CMC (single flow)
// or must be split via the software per-VL-ID counters (shared queue).
static uint16_t cmc_flows_on_srv_rx(uint16_t srv_rx_port, uint16_t srv_rx_queue)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < CMC_PORT_COUNT; i++) {
        if (cmc_port_map[i].tx_server_port == srv_rx_port &&
            cmc_port_map[i].tx_server_queue == srv_rx_queue) {
            n++;
        }
    }
    return n;
}

static uint16_t cmc_flows_on_srv_tx(uint16_t srv_tx_port, uint16_t srv_tx_queue)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < CMC_PORT_COUNT; i++) {
        if (cmc_port_map[i].rx_server_port == srv_tx_port &&
            cmc_port_map[i].rx_server_queue == srv_tx_queue) {
            n++;
        }
    }
    return n;
}

static void print_cmc_table_group(const uint16_t *indices, uint16_t count,
                                  const struct rte_eth_stats port_hw_stats[])
{
    // Payload Verification now has 8 stat columns (Good, Bad, SplitMix64
    // Fail, CRC32 Fail, XOR Fail, Loss, Bit Error, BER) — XOR Fail was added
    // for the 1-byte XOR-zone check the CMC applies after CRC32.
    printf("  ┌─────────┬────────┬─────────────────────────────────────────────────────────────────────┬─────────────────────────────────────────────────────────────────────┬──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐\n");
    printf("  │ Server  │  CMC   │                          CMC TX (CMC→Server)                        │                          CMC RX (Server→CMC)                        │                                                  Payload Verification                                                                                  │\n");
    printf("  │  Port   │  Port  ├─────────────────────┬─────────────────────┬─────────────────────────┼─────────────────────┬─────────────────────┬─────────────────────────┼─────────────────────┬─────────────────────┬─────────────────────┬─────────────────────┬─────────────────────┬─────────────────────┬─────────────────────┬─────────────┤\n");
    printf("  │         │        │       Packets       │        Bytes        │          Gbps           │       Packets       │        Bytes        │          Gbps           │        Good         │         Bad         │  SplitMix64 Fail    │    CRC32 Fail       │      XOR Fail       │        Loss         │      Bit Error      │     BER     │\n");
    printf("  ├─────────┼────────┼─────────────────────┼─────────────────────┼─────────────────────────┼─────────────────────┼─────────────────────┼─────────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────┤\n");

    for (uint16_t i = 0; i < count; i++) {
        uint16_t cmc = indices[i];
        const struct cmc_port_map_entry *entry = &cmc_port_map[cmc];

        uint16_t srv_rx_port = entry->tx_server_port;
        uint16_t srv_rx_queue = entry->tx_server_queue;
        uint16_t srv_tx_port = entry->rx_server_port;
        uint16_t srv_tx_queue = entry->rx_server_queue;

        uint64_t cmc_tx_pkts, cmc_tx_bytes;
        uint64_t cmc_rx_pkts, cmc_rx_bytes;

        // CMC TX (CMC→Server, server RX side).
        // Use HW q_ipackets/q_ibytes when this CMC is the only flow on that
        // RX queue; otherwise split via software per-CMC rx counters.
        if (cmc_flows_on_srv_rx(srv_rx_port, srv_rx_queue) == 1) {
            cmc_tx_pkts = port_hw_stats[srv_rx_port].q_ipackets[srv_rx_queue];
            cmc_tx_bytes = port_hw_stats[srv_rx_port].q_ibytes[srv_rx_queue];
        } else {
            cmc_tx_pkts = rte_atomic64_read(&cmc_stats[cmc].total_rx_pkts);
            cmc_tx_bytes = cmc_tx_pkts * (uint64_t)PACKET_SIZE;
        }

        // CMC RX (Server→CMC, server TX side).
        // Use HW q_opackets/q_obytes for dedicated queues; otherwise sum the
        // per-VL-ID TX sequence counters over this CMC's TX range.
        if (cmc_flows_on_srv_tx(srv_tx_port, srv_tx_queue) == 1) {
            cmc_rx_pkts = port_hw_stats[srv_tx_port].q_opackets[srv_tx_queue];
            cmc_rx_bytes = port_hw_stats[srv_tx_port].q_obytes[srv_tx_queue];
        } else {
            uint64_t sum = 0;
            for (uint16_t v = 0; v < entry->vl_id_count; v++) {
                sum += get_tx_vl_sequence(srv_tx_port,
                                          (uint16_t)(entry->tx_vl_id_start + v));
            }
            cmc_rx_pkts = sum;
            cmc_rx_bytes = sum * (uint64_t)PACKET_SIZE;
        }

        // Gbps delta calculation
        uint64_t tx_delta = cmc_tx_bytes - cmc_prev_tx_bytes[cmc];
        uint64_t rx_delta = cmc_rx_bytes - cmc_prev_rx_bytes[cmc];
        double tx_gbps = to_gbps(tx_delta);
        double rx_gbps = to_gbps(rx_delta);

        // Update prev values
        cmc_prev_tx_bytes[cmc] = cmc_tx_bytes;
        cmc_prev_rx_bytes[cmc] = cmc_rx_bytes;

        // Payload verification statistics
        uint64_t good = rte_atomic64_read(&cmc_stats[cmc].good_pkts);
        uint64_t bad = rte_atomic64_read(&cmc_stats[cmc].bad_pkts);
        uint64_t sm_fail = rte_atomic64_read(&cmc_stats[cmc].splitmix_fail);
        uint64_t crc_fail = rte_atomic64_read(&cmc_stats[cmc].crc32_fail);
        uint64_t xor_fail = rte_atomic64_read(&cmc_stats[cmc].xor_fail);
        uint64_t lost = rte_atomic64_read(&cmc_stats[cmc].lost_pkts);
        uint64_t bit_errors_raw = rte_atomic64_read(&cmc_stats[cmc].bit_errors);

#if IMIX_ENABLED
        uint64_t lost_bits = lost * (uint64_t)IMIX_AVG_PACKET_SIZE * 8;
#else
        uint64_t lost_bits = lost * (uint64_t)PACKET_SIZE * 8;
#endif
        uint64_t bit_errors = bit_errors_raw + lost_bits;

        double ber = 0.0;
        uint64_t total_bits = cmc_tx_bytes * 8 + lost_bits;
        if (total_bits > 0) {
            ber = (double)bit_errors / (double)total_bits;
        }

        printf("  │    %u    │ %-6s │ %19lu │ %19lu │ %23.4f │ %19lu │ %19lu │ %23.4f │ %19lu │ %19lu │ %19lu │ %19lu │ %19lu │ %19lu │ %19lu │ %11.2e │\n",
               entry->rx_server_port,
               cmc_port_labels[cmc],
               cmc_tx_pkts, cmc_tx_bytes, tx_gbps,
               cmc_rx_pkts, cmc_rx_bytes, rx_gbps,
               good, bad, sm_fail, crc_fail, xor_fail, lost, bit_errors, ber);
    }

    printf("  └─────────┴────────┴─────────────────────┴─────────────────────┴─────────────────────────┴─────────────────────┴─────────────────────┴─────────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────┘\n");
}

static void helper_print_cmc_stats(const struct ports_config *ports_config,
                                   bool warmup_complete, unsigned loop_count,
                                   unsigned test_time)
{
    // Clear the screen
    if (!g_daemon_mode) {
        printf("\033[2J\033[H");
    } else {
        printf("\n========== [%s %u sec] ==========\n",
               warmup_complete ? "TEST" : "WARM-UP",
               warmup_complete ? test_time : loop_count);
    }

    // Header
    printf("╔══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n");
    if (!warmup_complete) {
        printf("║                                                              CMC PORT STATS - WARM-UP (%3u/120 sec)                                                                                                                                  ║\n", loop_count);
    } else {
        printf("║                                                              CMC PORT STATS - TEST Duration: %5u sec                                                                                                                                 ║\n", test_time);
    }
    printf("╚══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n");

    // Fetch HW stats once (per port)
    struct rte_eth_stats port_hw_stats[MAX_PORTS];
    for (uint16_t i = 0; i < ports_config->nb_ports; i++) {
        uint16_t port_id = ports_config->ports[i].port_id;
        if (rte_eth_stats_get(port_id, &port_hw_stats[port_id]) != 0) {
            memset(&port_hw_stats[port_id], 0, sizeof(struct rte_eth_stats));
        }
    }

    // Table 1: Network A (VLAN 97 → 225, SRC MAC tail 0x20)
    printf("\n  === Network A (VLAN 97 -> 225 | VL 10001..10104 -> 10521..10624 | SRC MAC tail 0x20) ===\n");
    print_cmc_table_group(neta_cmc_indices, NETA_COUNT, port_hw_stats);

    // Table 2: Network B (VLAN 98 → 226, SRC MAC tail 0x40)
    printf("\n  === Network B (VLAN 98 -> 226 | VL 10001..10104 -> 10521..10624 | SRC MAC tail 0x40) ===\n");
    print_cmc_table_group(netb_cmc_indices, NETB_COUNT, port_hw_stats);

    // Warnings
    bool has_warning = false;
    for (uint16_t cmc = 0; cmc < CMC_DPDK_PORT_COUNT; cmc++) {
        uint64_t bad = rte_atomic64_read(&cmc_stats[cmc].bad_pkts);
        uint64_t sm_fail = rte_atomic64_read(&cmc_stats[cmc].splitmix_fail);
        uint64_t crc_fail = rte_atomic64_read(&cmc_stats[cmc].crc32_fail);
        uint64_t xor_fail = rte_atomic64_read(&cmc_stats[cmc].xor_fail);
        uint64_t bit_err = rte_atomic64_read(&cmc_stats[cmc].bit_errors);
        uint64_t lost = rte_atomic64_read(&cmc_stats[cmc].lost_pkts);

        if (bad > 0 || bit_err > 0 || lost > 0) {
            if (!has_warning) {
                printf("\n  WARNINGS:\n");
                has_warning = true;
            }
            if (bad > 0)
                printf("      %s (CMC %u): %lu bad packets! (SM:%lu CRC:%lu XOR:%lu)\n",
                       cmc_port_labels[cmc], cmc, bad, sm_fail, crc_fail, xor_fail);
            if (bit_err > 0)
                printf("      %s (CMC %u): %lu bit errors!\n", cmc_port_labels[cmc], cmc, bit_err);
            if (lost > 0)
                printf("      %s (CMC %u): %lu lost packets!\n", cmc_port_labels[cmc], cmc, lost);
        }
    }

    printf("\n  Press Ctrl+C to stop\n");
}
#endif /* STATS_MODE_CMC */

// ==========================================
// SERVER PORT-BASED STATISTICS TABLE (Legacy table)
// ==========================================
static void helper_print_server_stats(const struct ports_config *ports_config,
                                      const uint64_t prev_tx_bytes[],
                                      const uint64_t prev_rx_bytes[],
                                      bool warmup_complete, unsigned loop_count,
                                      unsigned test_time)
{
    // Clear screen (only in interactive mode, disabled in daemon mode for log files)
    if (!g_daemon_mode) {
        printf("\033[2J\033[H");
    } else {
        // Daemon mode: separator line between tables
        printf("\n========== [%s %u sec] ==========\n",
               warmup_complete ? "TEST" : "WARM-UP",
               warmup_complete ? test_time : loop_count);
    }

    // Header (240 characters wide)
    printf("╔══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n");
    if (!warmup_complete) {
        printf("║                                                                    WARM-UP PHASE (%3u/120 sec) - Statistics will be reset at 120 seconds                                                                                        ║\n", loop_count);
    } else {
        printf("║                                                                    TEST IN PROGRESS - Test Duration: %5u sec                                                                                                                   ║\n", test_time);
    }
    printf("╚══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝\n\n");

    // Main statistics table (240 characters)
    printf("┌──────┬─────────────────────────────────────────────────────────────────────┬─────────────────────────────────────────────────────────────────────┬───────────────────────────────────────────────────────────────────────────────────────────────────┐\n");
    printf("│ Port │                            TX (Transmitted)                          │                            RX (Received)                            │                                      PRBS Verification                                              │\n");
    printf("│      ├─────────────────────┬─────────────────────┬─────────────────────────┼─────────────────────┬─────────────────────┬─────────────────────────┼─────────────────────┬─────────────────────┬─────────────────────┬─────────────────────┬─────────────┤\n");
    printf("│      │       Packets       │        Bytes        │          Gbps           │       Packets       │        Bytes        │          Gbps           │        Good         │         Bad         │        Lost         │      Bit Error      │     BER     │\n");
    printf("├──────┼─────────────────────┼─────────────────────┼─────────────────────────┼─────────────────────┼─────────────────────┼─────────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────────────┼─────────────┤\n");

    struct rte_eth_stats st;

    for (uint16_t i = 0; i < ports_config->nb_ports; i++) {
        uint16_t port_id = ports_config->ports[i].port_id;

        if (rte_eth_stats_get(port_id, &st) != 0) {
            printf("│  %2u  │         N/A         │         N/A         │           N/A           │         N/A         │         N/A         │           N/A           │         N/A         │         N/A         │         N/A         │         N/A         │     N/A     │\n", port_id);
            continue;
        }

        // HW statistics
        uint64_t tx_pkts = st.opackets;
        uint64_t tx_bytes = st.obytes;
        uint64_t rx_pkts = st.ipackets;
        uint64_t rx_bytes = st.ibytes;

        // Per-second rate calculation
        uint64_t tx_bytes_delta = tx_bytes - prev_tx_bytes[port_id];
        uint64_t rx_bytes_delta = rx_bytes - prev_rx_bytes[port_id];
        double tx_gbps = to_gbps(tx_bytes_delta);
        double rx_gbps = to_gbps(rx_bytes_delta);

        // PRBS verification statistics
        uint64_t good = rte_atomic64_read(&rx_stats_per_port[port_id].good_pkts);
        uint64_t bad = rte_atomic64_read(&rx_stats_per_port[port_id].bad_pkts);
        uint64_t lost = rte_atomic64_read(&rx_stats_per_port[port_id].lost_pkts);
        uint64_t bit_errors_raw = rte_atomic64_read(&rx_stats_per_port[port_id].bit_errors);

        // Include lost packets in bit_errors (each lost packet = all bits erroneous)
#if IMIX_ENABLED
        uint64_t lost_bits = lost * (uint64_t)IMIX_AVG_PACKET_SIZE * 8;
#else
        uint64_t lost_bits = lost * (uint64_t)PACKET_SIZE * 8;
#endif
        uint64_t bit_errors = bit_errors_raw + lost_bits;

        // Bit Error Rate (BER) calculation (lost packet bits added to total)
        double ber = 0.0;
        uint64_t total_bits = rx_bytes * 8 + lost_bits;
        if (total_bits > 0) {
            ber = (double)bit_errors / (double)total_bits;
        }

        // Print table row
        printf("│  %2u  │ %19lu │ %19lu │ %23.4f │ %19lu │ %19lu │ %23.4f │ %19lu │ %19lu │ %19lu │ %19lu │ %11.2e │\n",
               port_id,
               tx_pkts, tx_bytes, tx_gbps,
               rx_pkts, rx_bytes, rx_gbps,
               good, bad, lost, bit_errors, ber);
    }

    printf("└──────┴─────────────────────┴─────────────────────┴─────────────────────────┴─────────────────────┴─────────────────────┴─────────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────────────┴─────────────┘\n");

    // Warnings
    bool has_warning = false;
    for (uint16_t i = 0; i < ports_config->nb_ports; i++) {
        uint16_t port_id = ports_config->ports[i].port_id;

        uint64_t bad_pkts = rte_atomic64_read(&rx_stats_per_port[port_id].bad_pkts);
        uint64_t bit_errors = rte_atomic64_read(&rx_stats_per_port[port_id].bit_errors);
        uint64_t lost_pkts = rte_atomic64_read(&rx_stats_per_port[port_id].lost_pkts);

        if (bad_pkts > 0 || bit_errors > 0 || lost_pkts > 0) {
            if (!has_warning) {
                printf("\n  WARNINGS:\n");
                has_warning = true;
            }
            if (bad_pkts > 0) {
                printf("      Port %u: %lu bad packets detected!\n", port_id, bad_pkts);
            }
            if (bit_errors > 0) {
                printf("      Port %u: %lu bit errors detected!\n", port_id, bit_errors);
            }
            if (lost_pkts > 0) {
                printf("      Port %u: %lu lost packets detected!\n", port_id, lost_pkts);
            }
        }

        // HW missed packets check
        struct rte_eth_stats st2;
        if (rte_eth_stats_get(port_id, &st2) == 0 && st2.imissed > 0) {
            if (!has_warning) {
                printf("\n  WARNINGS:\n");
                has_warning = true;
            }
            printf("      Port %u: %lu packets missed by hardware (imissed)!\n", port_id, st2.imissed);
        }
    }

    printf("\n  Press Ctrl+C to stop\n");
}

// ==========================================
// PUBLIC API: helper_print_stats
// ==========================================
// CMC mode (STATS_MODE_CMC=1): per-network 2-table CMC display (Net A / Net B).
// Both ATE mode and unit-test mode use the same layout — the only difference
// is verification semantics, which are captured in the same per-CMC counters
// (Good/Bad/Lost/Bit Error). The SplitMix64 / CRC32 / XOR Fail columns stay
// at zero in ATE mode by design (the pure-PRBS path doesn't touch them).
// Legacy (STATS_MODE_CMC=0): single port-aggregate table.

void helper_print_stats(const struct ports_config *ports_config,
                        const uint64_t prev_tx_bytes[], const uint64_t prev_rx_bytes[],
                        bool warmup_complete, unsigned loop_count, unsigned test_time)
{
#if STATS_MODE_CMC
    helper_print_cmc_stats(ports_config, warmup_complete, loop_count, test_time);
    (void)prev_tx_bytes;
    (void)prev_rx_bytes;
#else
    helper_print_server_stats(ports_config, prev_tx_bytes, prev_rx_bytes,
                              warmup_complete, loop_count, test_time);
#endif
}