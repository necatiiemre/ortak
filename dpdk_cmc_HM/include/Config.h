#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#define stringify(x) #x

// ==========================================
// TOKEN BUCKET TX MODE (must be defined early, used throughout)
// ==========================================
// 0 = Smooth pacing mode (rate limiter based)
// 1 = Token bucket mode: 1 packet from each VL-IDX every 1ms
// CMC default: smooth pacing (single TARGET_GBPS shared across the two
// networks). Token-bucket scaffolding is preserved so it can be flipped on
// later without a structural rewrite.
#ifndef TOKEN_BUCKET_TX_ENABLED
#define TOKEN_BUCKET_TX_ENABLED 0
#endif

#if TOKEN_BUCKET_TX_ENABLED
// CMC has 104 VL-IDs per VLAN on each TX queue — token-bucket bookkeeping
// uses these counts directly.
#define TB_VL_RANGE_SIZE_DEFAULT 104
#define TB_VL_RANGE_SIZE_NO_EXT  104
#define GET_TB_VL_RANGE_SIZE(port_id) TB_VL_RANGE_SIZE_DEFAULT

// Token bucket window (ms) - can be fractional (e.g., 1.0, 1.4, 2.5)
#define TB_WINDOW_MS 1.05
#define TB_PACKETS_PER_VL_PER_WINDOW 1
#endif

// ==========================================
// LATENCY TEST CONFIGURATION (compiled out by default)
// ==========================================
// Kept off in CMC; preserved for future use.

#ifndef LATENCY_TEST_ENABLED
#define LATENCY_TEST_ENABLED 0
#endif

#define LATENCY_TEST_TIMEOUT_SEC 5    // Packet wait timeout (seconds)
#define LATENCY_TEST_PACKET_SIZE 1518 // Test packet size (MAX)

// ==========================================
// IMIX (Internet Mix) CONFIGURATION (compiled out by default)
// ==========================================
// Custom IMIX profile preserved verbatim from the CMC heritage. Default off
// in CMC; flip IMIX_ENABLED to 1 if needed for future tests.
#define IMIX_ENABLED 0

#define IMIX_SIZE_1 100
#define IMIX_SIZE_2 200
#define IMIX_SIZE_3 400
#define IMIX_SIZE_4 800
#define IMIX_SIZE_5 1200
#define IMIX_SIZE_6 1518

#define IMIX_PATTERN_SIZE 10
#define IMIX_AVG_PACKET_SIZE 964
#define IMIX_MIN_PACKET_SIZE IMIX_SIZE_1
#define IMIX_MAX_PACKET_SIZE IMIX_SIZE_6

#define IMIX_PATTERN_INIT {                             \
    IMIX_SIZE_1, IMIX_SIZE_2, IMIX_SIZE_3, IMIX_SIZE_4, \
    IMIX_SIZE_5, IMIX_SIZE_5, IMIX_SIZE_5,              \
    IMIX_SIZE_6, IMIX_SIZE_6, IMIX_SIZE_6}

// ==========================================
// VLAN & VL-ID MAPPING
// ==========================================
//
// CMC layout (single server port, two networks):
//   TX queue 0 → VLAN  97  / VL-IDX 10001..10104  (Network A, SRC MAC tail 0x20)
//   TX queue 1 → VLAN  98  / VL-IDX 10001..10104  (Network B, SRC MAC tail 0x40)
//   RX queue 0 → VLAN 225  / VL-IDX 10521..10624  (Network A return)
//   RX queue 1 → VLAN 226  / VL-IDX 10521..10624  (Network B return)
//
// VL-ID is encoded in the last 2 bytes of DST MAC (03:00:00:00:VV:VV) and
// in the last 2 bytes of DST IP (224.224.VV.VV). The "network type" is set
// in the last byte of the SRC MAC (0x20 = Net A, 0x40 = Net B); the CMC is
// expected to return packets on a network-specific RX VLAN, which is what
// the server uses to classify Net A vs Net B in stats.
//
// VL-IDs are intentionally identical across the two networks — the RX
// dispatcher keys on (port, queue) rather than VL-ID alone, so the overlap
// is unambiguous on the server side.

typedef struct
{
  uint16_t start; // inclusive
  uint16_t end;   // exclusive
} vlid_range_t;

// ==========================================
/* VLAN CONFIGURATION */
// ==========================================
#define MAX_TX_VLANS_PER_PORT 32
#define MAX_RX_VLANS_PER_PORT 32
#define MAX_PORTS_CONFIG 16

struct port_vlan_config
{
  uint16_t tx_vlans[MAX_TX_VLANS_PER_PORT]; // VLAN header tags
  uint16_t tx_vlan_count;
  uint16_t rx_vlans[MAX_RX_VLANS_PER_PORT]; // VLAN header tags
  uint16_t rx_vlan_count;

  uint16_t tx_vl_ids[MAX_TX_VLANS_PER_PORT]; // VL-ID start per queue
  uint16_t rx_vl_ids[MAX_RX_VLANS_PER_PORT]; // VL-ID start per queue

  // Per-queue VL-ID count (0 = use default VL_RANGE_SIZE_PER_QUEUE)
  uint16_t tx_vl_counts[MAX_TX_VLANS_PER_PORT];
};

// CMC normal-mode VLAN/VL-ID template. Only the first slot (DPDK port_id 0,
// the EAL-allowlisted server port 2) carries traffic; the remaining slots
// are zero-initialized and unused (kept so MAX_PORTS_CONFIG can stay 16
// without changing the rest of the config plumbing).
#define PORT_VLAN_CONFIG_INIT                                                   \
  {                                                                             \
    {.tx_vlans      = {97, 98},                                                 \
     .tx_vlan_count = 2,                                                        \
     .rx_vlans      = {225, 226},                                               \
     .rx_vlan_count = 2,                                                        \
     .tx_vl_ids     = {10001, 10001},                                           \
     .rx_vl_ids     = {10521, 10521},                                           \
     .tx_vl_counts  = {104, 104}},                                              \
  }

// ==========================================
// ATE TEST MODE - PORT VLAN CONFIGURATION (Loopback)
// ==========================================
// Same shape as normal mode for now. ATE mode is preserved as scaffolding —
// edit this when CMC ATE flows diverge from the unit-test flows.

#define ATE_PORT_VLAN_CONFIG_INIT                                               \
  {                                                                             \
    {.tx_vlans      = {97, 98},                                                 \
     .tx_vlan_count = 2,                                                        \
     .rx_vlans      = {225, 226},                                               \
     .rx_vlan_count = 2,                                                        \
     .tx_vl_ids     = {10001, 10001},                                           \
     .rx_vl_ids     = {10001, 10001},                                           \
     .tx_vl_counts  = {104, 104}},                                              \
  }

// ==========================================
// TX/RX CORE CONFIGURATION
// ==========================================
// (Can be overridden via Makefile)
#ifndef NUM_TX_CORES
#define NUM_TX_CORES 2
#endif

#ifndef NUM_RX_CORES
#define NUM_RX_CORES 2
#endif

// ==========================================
// TARGET RATE
// ==========================================
// Single target rate for the whole port (split across the two TX queues).
// The previous CROSS_TARGET_GBPS / fast-mid-slow distinction is gone — CMC
// has one rate to tune.

#ifndef TARGET_GBPS
#define TARGET_GBPS 1.0
#endif

#ifndef RATE_LIMITER_ENABLED
#define RATE_LIMITER_ENABLED 1
#endif

// Queue counts equal core counts
#define NUM_TX_QUEUES_PER_PORT NUM_TX_CORES
#define NUM_RX_QUEUES_PER_PORT NUM_RX_CORES

// ==========================================
// PACKET CONFIGURATION (Fixed fields)
// ==========================================
#define DEFAULT_TTL 1
#define DEFAULT_TOS 0
#define DEFAULT_VLAN_PRIORITY 0

// MAC/IP templates. The SRC MAC last byte is overwritten per TX VLAN
// (0x20 = Network A, 0x40 = Network B); start_txrx_workers() is the single
// place that decides which value goes with which VLAN.
#define DEFAULT_SRC_MAC "02:00:00:00:00:20"
#define DEFAULT_DST_MAC_PREFIX "03:00:00:00" // Last 2 bytes = VL-ID

#define DEFAULT_SRC_IP "10.0.0.0"       // Fixed source IP
#define DEFAULT_DST_IP_PREFIX "224.224" // Last 2 bytes = VL-ID

// Network-type byte (set on SRC MAC[5] by the TX worker). The constants are
// referenced both when building the packet and when documenting the RX-side
// classification.
#define CMC_NET_A_SRC_MAC_TAIL 0x20
#define CMC_NET_B_SRC_MAC_TAIL 0x40

// UDP ports
#define DEFAULT_SRC_PORT 100
#define DEFAULT_DST_PORT 100

// ==========================================
// STATISTICS CONFIGURATION
// ==========================================
#define STATS_INTERVAL_SEC 1 // Write statistics every N seconds

// ==========================================
// CMC PORT-BASED STATISTICS MODE
// ==========================================
// STATS_MODE_CMC=1: per-flow statistics table (Network A + Network B)
//   - RX queue steering: rte_flow VLAN match (each queue = 1 VLAN = 1 flow)
//   - HW per-queue stats drive the Gbps calculation
//   - SplitMix64 + CRC32C + PRBS validation per network
//
// STATS_MODE_CMC=0: legacy per-server-port table (single port).

#ifndef STATS_MODE_CMC
#define STATS_MODE_CMC 1
#endif

// CMC has exactly two flows: Network A (VLAN 97 → 225) and Network B (VLAN
// 98 → 226). Each occupies one CMC entry / one TX queue / one RX queue.
#define CMC_PORT_COUNT 2
#define CMC_DPDK_PORT_COUNT 2

// 1 VLAN per CMC port (1:1 with RX queue)
#define CMC_VLANS_PER_PORT 1

// Payload verification mode for a CMC flow (used on server RX). CMC normal
// mode is SplitMix64+CRC32C+PRBS; ATE mode falls back to plain PRBS.
#define CMC_PAYLOAD_SPLITMIX_CRC 0  // CMC applies SplitMix64 XOR + CRC32C
#define CMC_PAYLOAD_PURE_PRBS    1  // ATE / loopback: plain PRBS

// ==========================================
// CMC PORT MAPPING TABLE
// ==========================================
// One entry per directional flow. The CMC layout is two flows on a single
// server port (DPDK port_id 0, EAL-allowlisted server port 2):
//
//   CMC 0  Net A  TX VLAN  97 / VL 10001..10104
//                 RX VLAN 225 / VL 10521..10624  (SplitMix+CRC remap by CMC,
//                                                 VL-ID +520 shift)
//   CMC 1  Net B  TX VLAN  98 / VL 10001..10104
//                 RX VLAN 226 / VL 10521..10624  (same mode/shift)
//
// vl_id_start describes the range observed on the server RX side (what the
// CMC writes into the returning packet). tx_vl_id_start is what the server
// sends.

struct cmc_port_map_entry {
    uint16_t cmc_port_id;       // Logical CMC port index (0..CMC_PORT_COUNT-1)

    // CMC RX (Server → CMC): server sends from this VLAN
    uint16_t rx_vlan;
    uint16_t rx_server_port;
    uint16_t rx_server_queue;

    // CMC TX (CMC → Server): CMC sends from this VLAN
    uint16_t tx_vlan;
    uint16_t tx_server_port;
    uint16_t tx_server_queue;

    // VL-ID range (server-RX side) and the TX-side start for the same flow.
    uint16_t vl_id_start;
    uint16_t tx_vl_id_start;
    uint16_t vl_id_count;

    // Payload verification mode (see CMC_PAYLOAD_* above)
    uint8_t  payload_mode;
};

#define CMC_PORT_MAP_INIT {                                                                       \
    /* CMC 0: Network A — VLAN 97 ↔ 225, VL 10001..10104 ↔ 10521..10624 */                        \
    {.cmc_port_id = 0,  .rx_vlan = 97,  .rx_server_port = 0, .rx_server_queue = 0,                \
                         .tx_vlan = 225, .tx_server_port = 0, .tx_server_queue = 0,               \
                         .vl_id_start = 10521, .tx_vl_id_start = 10001, .vl_id_count = 104,       \
                         .payload_mode = CMC_PAYLOAD_SPLITMIX_CRC},                               \
    /* CMC 1: Network B — VLAN 98 ↔ 226, VL 10001..10104 ↔ 10521..10624 */                        \
    {.cmc_port_id = 1,  .rx_vlan = 98,  .rx_server_port = 0, .rx_server_queue = 1,                \
                         .tx_vlan = 226, .tx_server_port = 0, .tx_server_queue = 1,               \
                         .vl_id_start = 10521, .tx_vl_id_start = 10001, .vl_id_count = 104,       \
                         .payload_mode = CMC_PAYLOAD_SPLITMIX_CRC},                               \
}

// VLAN → CMC port lookup (fast access). VLANs 0..256 are 8-bit indexable.
#define CMC_VLAN_LOOKUP_SIZE 257
#define CMC_VLAN_INVALID 0xFF

// (Kept for diagnostic-only paths; per-packet dispatch uses queue_to_cmc_port)
#define CMC_VL_ID_INVALID 0xFFFF

// ==========================================
// CMC PORT DISPLAY GROUPING (NETWORK A / NETWORK B)
// ==========================================
// Two display tables, one per network. The grouping arrays live here so the
// stats layer (Helpers.c) can iterate without re-deriving them.

#define NETA_COUNT 1
#define NETB_COUNT 1

static const uint16_t neta_cmc_indices[NETA_COUNT] = {0};
static const uint16_t netb_cmc_indices[NETB_COUNT] = {1};

// Display label per CMC port (indexed by CMC port number)
static const char * const cmc_port_labels[CMC_PORT_COUNT] = {
    "NET-A",   /* CMC 0: Network A (VLAN 97 → 225) */
    "NET-B",   /* CMC 1: Network B (VLAN 98 → 226) */
};

#endif /* CONFIG_H */
