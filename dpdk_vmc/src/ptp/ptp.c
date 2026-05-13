#define _GNU_SOURCE
/* PTPv2 (IEEE 1588) over raw Ethernet — two mechanisms:
 *   M1 (Master role) — passively timestamps Sync (ATE→VMC) and Delay_Req
 *                      (Unit→VMC); answers Delay_Req with Delay_Resp.
 *   M2 (Slave  role) — receives Sync (VMC→), emits Delay_Req on every Sync,
 *                      timestamps Delay_Resp (VMC→).
 *
 * Both mechanisms share one dedicated RX queue and one dedicated TX queue per
 * carrying NIC port (see PTP_RX_QUEUE_ID / PTP_TX_QUEUE_ID). EtherType=0x88F7
 * is steered to the PTP RX queue via an rte_flow rule installed after port
 * start. VLAN tag is written inline by build_eth_ptp (PRBS-style — no HW
 * offload), since RTE_ETH_TX_OFFLOAD_VLAN_INSERT is not negotiated on the
 * shared port_conf used by the PRBS data plane. */

#include "ptp.h"
#include "ptp_wire.h"
#include "Port.h"
#include "Config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_flow.h>
#include <rte_mempool.h>
#include <rte_byteorder.h>
#include <rte_ether.h>

/* ------------------------------------------------------------------ */
/* Flow table                                                          */
/* ------------------------------------------------------------------ */

const struct ptp_flow ptp_flows[PTP_NUM_FLOWS] = {
    /* Mechanism 1 — Master role (Port 2 carries both NEs) */
    { PTP_ROLE_M1_MASTER, 2, PTP_NE_A, 100, 1500,  228, 1502,  100, 1504, "J4-4" },
    { PTP_ROLE_M1_MASTER, 2, PTP_NE_B,  97, 1501,  225, 1503,   97, 1505, "J4-1" },

    /* Mechanism 2 — Slave role (Port 1 = NE A via J7-4, Port 3 = NE B via J5-4) */
    { PTP_ROLE_M2_SLAVE,  1, PTP_NE_A, 240, 1600,  112, 1602,  240, 1604, "J7-4" },
    { PTP_ROLE_M2_SLAVE,  3, PTP_NE_B, 232, 1601,  104, 1603,  232, 1605, "J5-4" },
};

/* ------------------------------------------------------------------ */
/* Per-flow runtime state                                              */
/* ------------------------------------------------------------------ */

struct ptp_state {
    /* Last PTP timestamps captured (CLOCK_REALTIME ns). T1/T3 come from the
     * peer's PTP payload; T2/T4 are our local capture (M1: T2 on Sync RX,
     * T4 on Delay_Req RX; M2: T2 on Sync RX, T4 from Delay_Resp). */
    uint64_t last_t1_ns;
    uint64_t last_t2_ns;
    uint64_t last_t3_ns;
    uint64_t last_t4_ns;

    /* M2 watchdog (MONOTONIC ns) */
    uint64_t last_req_tx_mono_ns;
    bool     req_outstanding;
    uint64_t resp_timeout;

    /* Counters — written only by the PTP thread, so plain uint64_t. */
    uint64_t sync_rx;
    uint64_t sync_tx;       /* M1 only */
    uint64_t req_rx;        /* M1 only */
    uint64_t resp_tx;       /* M1 only */
    uint64_t req_tx;        /* M2 only */
    uint64_t resp_rx;       /* M2 only */

    /* M1 Sync TX pacing (MONOTONIC ns) */
    uint64_t last_sync_tx_mono_ns;

    uint64_t bad_vlan;
    uint64_t bad_ne;
    uint64_t bad_size;
    uint64_t bad_msg_type;
    uint64_t alloc_fail;
    uint64_t tx_fail;

    /* Sync staleness (MONOTONIC) */
    uint64_t last_sync_rx_mono_ns;
    bool     sync_stale;

    /* PTPv2 sequence counters — independent per direction. We do not echo
     * incoming sequence numbers (the user requested no per-message matching);
     * the counter only serves as a unique sequenceId on the wire. */
    uint16_t tx_seq;
};

static struct ptp_state g_state[PTP_NUM_FLOWS];

/* Diagnostics — budget-capped logging of unexpected RX on the PTP queue, plus
 * one-shot "first Sync seen" markers per flow. Helps diagnose silent ports
 * (e.g. port 2 receiving no PTPv2 at all) without flooding the console. */
#define PTP_DBG_UNMATCHED_BUDGET 8
static uint32_t g_dbg_unmatched_left[PTP_NUM_FLOWS];   /* per port slot */
static bool     g_dbg_first_sync_done[PTP_NUM_FLOWS];  /* per flow */

/* ------------------------------------------------------------------ */
/* Module-wide state                                                   */
/* ------------------------------------------------------------------ */

static struct rte_mempool *g_ptp_mempool = NULL;
static pthread_t           g_ptp_thread;
static volatile bool       g_ptp_running = false;
static volatile bool       g_ptp_stop    = false;

/* ------------------------------------------------------------------ */
/* Time helpers                                                        */
/* ------------------------------------------------------------------ */

static inline uint64_t now_real_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline uint64_t now_mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/* Flow lookups                                                        */
/* ------------------------------------------------------------------ */

bool ptp_port_has_flow(uint16_t port_id)
{
    for (int i = 0; i < PTP_NUM_FLOWS; i++) {
        if (ptp_flows[i].dpdk_port == port_id)
            return true;
    }
    return false;
}

/* Match incoming packet to a flow by (port, VL-IDX, msg_type).
 * Returns flow index in [0, PTP_NUM_FLOWS) or -1 if no match. */
static int lookup_flow(uint16_t port_id, uint16_t vl_id, uint8_t msg_type)
{
    for (int i = 0; i < PTP_NUM_FLOWS; i++) {
        const struct ptp_flow *f = &ptp_flows[i];
        if (f->dpdk_port != port_id) continue;

        switch (msg_type) {
        case PTP_MSG_SYNC:
            if (f->sync_vl_id == vl_id) return i;
            break;
        case PTP_MSG_DELAY_REQ:
            /* Only M1 receives Delay_Req. */
            if (f->role == PTP_ROLE_M1_MASTER && f->req_vl_id == vl_id) return i;
            break;
        case PTP_MSG_DELAY_RESP:
            /* Only M2 receives Delay_Resp. */
            if (f->role == PTP_ROLE_M2_SLAVE && f->resp_vl_id == vl_id) return i;
            break;
        default:
            break;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Frame builders                                                      */
/* ------------------------------------------------------------------ */

/* Fill the PTPv2 common header. Caller must set messageLength, sequenceId,
 * control, and the body that follows. */
static void fill_ptp_header(struct ptp_header *h,
                            uint8_t  msg_type,
                            uint16_t length,
                            uint8_t  domain,
                            uint16_t sequence_id,
                            uint8_t  control,
                            uint8_t  ne)
{
    memset(h, 0, sizeof(*h));
    h->ts_msg      = (uint8_t)(msg_type & 0x0F);     /* transportSpecific=0 */
    h->ver         = PTP_VERSION;                    /* reserved=0, ver=2  */
    h->length_be   = rte_cpu_to_be_16(length);
    h->domain      = domain;
    /* Mirror the peer master's flagField. On the wire this is octet0=0x01
     * (alternateMasterFlag), octet1=0x02 (leap59). The previous 0x0100 only
     * set octet0 and left octet1=0; some strict M1 slaves reject Sync without
     * the expected octet1 bits, which would explain why our Sync drew no
     * Delay_Req. */
    h->flags_be    = rte_cpu_to_be_16(0x0102);
    /* sourcePortIdentity: 8 bytes clockIdentity + 2 bytes portNumber.
     * Encode our local clockIdentity from the NE code so a wire capture is
     * self-describing. */
    h->source_port_id[0] = 0x02;
    h->source_port_id[7] = ne;
    h->source_port_id[8] = 0x00;
    h->source_port_id[9] = 0x01;
    h->sequence_id_be    = rte_cpu_to_be_16(sequence_id);
    h->control           = control;
    /* 1 Hz Sync → logMsgInterval = 0 (2^0 = 1 s). 0x7F is a "not meaningful"
     * sentinel that strict slaves treat as invalid and reject the message. */
    h->log_msg_interval  = 0x00;
}

/* Build an Ethernet + 802.1Q + PTPv2 frame in `dst` of total length
 * `body_len + sizeof(ptp_eth_frame)`. VLAN tag is written into the packet
 * bytes (no HW offload). SRC MAC follows the existing PRBS convention
 * (02:00:00:00:00:NE), the same encoding the Cumulus switch uses to identify
 * the local network element. */
static void build_eth_ptp(uint8_t      *dst,
                          uint8_t       ne,
                          uint16_t      vlan_id,
                          uint16_t      dst_vl_id,
                          const void   *ptp_body,
                          uint16_t      body_len)
{
    struct ptp_eth_frame *eth = (struct ptp_eth_frame *)dst;

    /* Destination MAC = 03:00:00:00 || vl_id (BE) */
    eth->dst_mac[0] = 0x03;
    eth->dst_mac[1] = 0x00;
    eth->dst_mac[2] = 0x00;
    eth->dst_mac[3] = 0x00;
    eth->dst_mac[4] = (uint8_t)((dst_vl_id >> 8) & 0xFF);
    eth->dst_mac[5] = (uint8_t)( dst_vl_id        & 0xFF);

    eth->src_mac[0] = 0x02;
    eth->src_mac[1] = 0x00;
    eth->src_mac[2] = 0x00;
    eth->src_mac[3] = 0x00;
    eth->src_mac[4] = 0x00;
    eth->src_mac[5] = ne;

    eth->vlan_tpid_be  = rte_cpu_to_be_16(PTP_VLAN_TPID);
    eth->vlan_tci_be   = rte_cpu_to_be_16((uint16_t)(vlan_id & 0x0FFF));
    eth->ether_type_be = rte_cpu_to_be_16(PTP_ETHERTYPE);
    memcpy(dst + sizeof(*eth), ptp_body, body_len);
}

/* Build a Sync frame carrying T1 (our TX time) in originTimestamp.
 * Master role only: ATE → VMC on f->sync_vl_id at 1 Hz. The wire layout
 * mirrors the peer master byte-for-byte: messageLength = 106 with a 72-byte
 * body (10B originTimestamp + 62B zero padding) — see PTP_SYNC_PADDED_LEN. */
static void build_sync(uint8_t *dst,
                       const struct ptp_flow *f,
                       uint16_t sequence_id,
                       uint64_t t1_ns)
{
    uint8_t body[PTP_SYNC_PADDED_LEN];
    memset(body, 0, sizeof(body));

    fill_ptp_header((struct ptp_header *)body,
                    PTP_MSG_SYNC,
                    PTP_SYNC_PADDED_LEN, /* messageLength = 106 */
                    /* domain  */ 10,
                    sequence_id,
                    /* control */ 0x00,
                    f->ne);

    ptp_ns_to_ts(t1_ns,
                 (struct ptp_timestamp *)(body + PTP_HDR_LEN));
    /* Remaining 62 bytes already zero from memset. */

    build_eth_ptp(dst, f->ne, f->sync_vlan, f->sync_vl_id,
                  body, sizeof(body));
}

/* Build a Delay_Resp frame echoing T3 as receiveTimestamp.
 * Per the user's spec (PTPv2 standard form): receiveTimestamp carries the
 * master's RX time of the Delay_Req — which is T3 echoed back, allowing the
 * slave to close the (T4-T3) leg. */
static void build_delay_resp(uint8_t *dst,
                             const struct ptp_flow *f,
                             uint16_t sequence_id,
                             uint64_t t3_ns,
                             const uint8_t *requesting_port_id)
{
    struct ptp_msg_delay_resp body;
    memset(&body, 0, sizeof(body));
    fill_ptp_header(&body.hdr, PTP_MSG_DELAY_RESP,
                    PTP_DELAY_RESP_LEN, /* domain */ 10,
                    sequence_id, /* control */ 0x03, f->ne);
    ptp_ns_to_ts(t3_ns, &body.receive_ts);
    if (requesting_port_id)
        memcpy(body.requesting_port_id, requesting_port_id, 10);

    build_eth_ptp(dst, f->ne, f->resp_vlan, f->resp_vl_id, &body, PTP_DELAY_RESP_LEN);
}

/* Build a Delay_Req frame carrying T3 in originTimestamp. */
static void build_delay_req(uint8_t *dst,
                            const struct ptp_flow *f,
                            uint16_t sequence_id,
                            uint64_t t3_ns)
{
    struct ptp_msg_sync body;
    memset(&body, 0, sizeof(body));
    fill_ptp_header(&body.hdr, PTP_MSG_DELAY_REQ,
                    PTP_DELAY_REQ_LEN, /* domain */ 10,
                    sequence_id, /* control */ 0x01, f->ne);
    ptp_ns_to_ts(t3_ns, &body.origin_ts);

    build_eth_ptp(dst, f->ne, f->req_vlan, f->req_vl_id, &body, PTP_DELAY_REQ_LEN);
}

/* Allocate a new mbuf, copy the supplied wire frame, pad to Ethernet min and
 * burst-TX on the dedicated PTP queue. Frame is expected to already include
 * the 802.1Q tag (written inline by build_eth_ptp). Returns 0 on success. */
static int ptp_tx_frame(uint16_t port_id,
                        const void *frame,
                        uint16_t frame_len)
{
    struct rte_mbuf *m = rte_pktmbuf_alloc(g_ptp_mempool);
    if (!m) return -1;

    /* Pad the wire frame up to 60 bytes (Ethernet min minus FCS). Some NICs
     * require this; cost is negligible. */
    uint16_t out_len = frame_len < 60 ? 60 : frame_len;
    uint8_t *out = (uint8_t *)rte_pktmbuf_append(m, out_len);
    if (!out) {
        rte_pktmbuf_free(m);
        return -1;
    }
    memcpy(out, frame, frame_len);
    if (out_len > frame_len)
        memset(out + frame_len, 0, out_len - frame_len);

    m->l2_len = sizeof(struct ptp_eth_frame);

    uint16_t sent = rte_eth_tx_burst(port_id, PTP_TX_QUEUE_ID, &m, 1);
    if (sent != 1) {
        rte_pktmbuf_free(m);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* RX dispatch                                                         */
/* ------------------------------------------------------------------ */

static void handle_sync_rx(int flow_idx,
                           const struct ptp_msg_sync *body,
                           uint64_t t2_ns,
                           uint64_t mono_now)
{
    struct ptp_state *st = &g_state[flow_idx];
    const struct ptp_flow *f = &ptp_flows[flow_idx];

    st->last_t1_ns           = ptp_ts_to_ns(&body->origin_ts);
    st->last_t2_ns           = t2_ns;
    st->sync_rx++;
    st->last_sync_rx_mono_ns = mono_now;
    st->sync_stale           = false;

    if (!g_dbg_first_sync_done[flow_idx]) {
        g_dbg_first_sync_done[flow_idx] = true;
        printf("[PTP-dbg] first Sync: flow=%d port=%u NE=%c vl_id=%u T1=%lu T2=%lu\n",
               flow_idx, (unsigned)f->dpdk_port,
               (f->ne == PTP_NE_A) ? 'A' : 'B',
               f->sync_vl_id,
               (unsigned long)st->last_t1_ns,
               (unsigned long)t2_ns);
    }

    /* In Slave mode, every Sync triggers a Delay_Req. */
    if (f->role == PTP_ROLE_M2_SLAVE) {
        /* If a previous Delay_Req is still outstanding when a new Sync
         * arrives, the master never answered it — count it as a timeout
         * before we overwrite the timer. The worker-thread watchdog only
         * fires when Sync stops; under steady 1 Hz traffic this path is
         * what actually surfaces missing Delay_Resp. */
        if (st->req_outstanding) {
            st->resp_timeout++;
            st->req_outstanding = false;
        }

        uint64_t t3 = now_real_ns();
        uint8_t  buf[sizeof(struct ptp_eth_frame) + PTP_DELAY_REQ_LEN];
        build_delay_req(buf, f, ++st->tx_seq, t3);
        if (ptp_tx_frame(f->dpdk_port, buf, sizeof(buf)) == 0) {
            st->last_t3_ns           = t3;
            st->req_tx++;
            st->last_req_tx_mono_ns  = mono_now;
            st->req_outstanding      = true;
        } else {
            st->tx_fail++;
        }
    }
}

static void handle_request_rx(int flow_idx,
                              const struct ptp_msg_sync *body,
                              uint64_t t4_ns)
{
    struct ptp_state *st = &g_state[flow_idx];
    const struct ptp_flow *f = &ptp_flows[flow_idx];

    uint64_t t3 = ptp_ts_to_ns(&body->origin_ts);
    st->last_t3_ns = t3;
    st->last_t4_ns = t4_ns;
    st->req_rx++;

    uint16_t seq = rte_be_to_cpu_16(body->hdr.sequence_id_be);
    uint8_t  buf[sizeof(struct ptp_eth_frame) + PTP_DELAY_RESP_LEN];
    build_delay_resp(buf, f, seq, t3, body->hdr.source_port_id);
    if (ptp_tx_frame(f->dpdk_port, buf, sizeof(buf)) == 0) {
        st->resp_tx++;
    } else {
        st->tx_fail++;
    }
}

static void handle_response_rx(int flow_idx,
                               const struct ptp_msg_delay_resp *body)
{
    struct ptp_state *st = &g_state[flow_idx];

    /* T4 = master's RX timestamp of our Delay_Req (echoed in receiveTimestamp).
     * The user requested no sequenceId matching, so we just close the most
     * recent outstanding request. */
    st->last_t4_ns = ptp_ts_to_ns(&body->receive_ts);
    st->resp_rx++;
    st->req_outstanding = false;
}

static void rx_one_port(uint16_t port_id)
{
    struct rte_mbuf *bufs[32];
    uint16_t n = rte_eth_rx_burst(port_id, PTP_RX_QUEUE_ID, bufs, 32);
    if (!n) return;

    uint64_t mono_now = now_mono_ns();

    for (uint16_t i = 0; i < n; i++) {
        struct rte_mbuf *m = bufs[i];
        uint64_t t_rx = now_real_ns();

        /* Minimum: ETH+VLAN (18B) + 34 PTP header */
        if (m->data_len < sizeof(struct ptp_eth_frame) + PTP_HDR_LEN) {
            /* attribute to no flow — count as bad on first matching port flow */
            for (int k = 0; k < PTP_NUM_FLOWS; k++)
                if (ptp_flows[k].dpdk_port == port_id) {
                    g_state[k].bad_size++;
                    break;
                }
            rte_pktmbuf_free(m);
            continue;
        }

        const uint8_t *p = rte_pktmbuf_mtod(m, const uint8_t *);
        const struct ptp_eth_frame *eth = (const struct ptp_eth_frame *)p;

        /* Dest MAC bytes 4..5 = VL-IDX (big-endian) */
        uint16_t vl_id   = ((uint16_t)eth->dst_mac[4] << 8) | eth->dst_mac[5];
        uint8_t  src_ne  = eth->src_mac[5];

        const struct ptp_header *hdr =
            (const struct ptp_header *)(p + sizeof(*eth));
        uint8_t msg_type = hdr->ts_msg & 0x0F;

        int idx = lookup_flow(port_id, vl_id, msg_type);
        if (idx < 0) {
            /* Steered to PTP queue but no flow matches — most likely unknown
             * VL-IDX or wrong msg type for this role. Account on the first
             * port flow as a generic bad_msg_type. */
            int port_slot = -1;
            for (int k = 0; k < PTP_NUM_FLOWS; k++)
                if (ptp_flows[k].dpdk_port == port_id) {
                    g_state[k].bad_msg_type++;
                    if (port_slot < 0) port_slot = k;
                    break;
                }
            if (port_slot >= 0 && g_dbg_unmatched_left[port_slot] > 0) {
                g_dbg_unmatched_left[port_slot]--;
                uint16_t vlan = rte_be_to_cpu_16(eth->vlan_tci_be) & 0x0FFF;
                printf("[PTP-dbg] unmatched port=%u msg=0x%02x vl_id=%u "
                       "vlan=%u src_ne=0x%02x len=%u\n",
                       (unsigned)port_id, msg_type, vl_id,
                       (unsigned)vlan, src_ne, (unsigned)m->data_len);
            }
            rte_pktmbuf_free(m);
            continue;
        }
        const struct ptp_flow *f = &ptp_flows[idx];

        /* VLAN is carried inline in the frame; read from packet bytes. */
        {
            uint16_t got_vlan = rte_be_to_cpu_16(eth->vlan_tci_be) & 0x0FFF;
            uint16_t want = (msg_type == PTP_MSG_SYNC)      ? f->sync_vlan
                          : (msg_type == PTP_MSG_DELAY_REQ) ? f->req_vlan
                          :                                   f->resp_vlan;
            if (got_vlan != (want & 0x0FFF)) {
                g_state[idx].bad_vlan++;
                rte_pktmbuf_free(m);
                continue;
            }
        }

        /* For Sync (M1 + M2), validate NE byte. Other directions originate
         * from the Unit/VMC and their src MAC convention differs. */
        if (msg_type == PTP_MSG_SYNC && src_ne != f->ne) {
            g_state[idx].bad_ne++;
            rte_pktmbuf_free(m);
            continue;
        }

        switch (msg_type) {
        case PTP_MSG_SYNC: {
            if (m->data_len < sizeof(*eth) + PTP_SYNC_LEN) {
                g_state[idx].bad_size++;
                break;
            }
            const struct ptp_msg_sync *body =
                (const struct ptp_msg_sync *)(p + sizeof(*eth));
            handle_sync_rx(idx, body, t_rx, mono_now);
            break;
        }
        case PTP_MSG_DELAY_REQ: {
            if (m->data_len < sizeof(*eth) + PTP_DELAY_REQ_LEN) {
                g_state[idx].bad_size++;
                break;
            }
            const struct ptp_msg_sync *body =
                (const struct ptp_msg_sync *)(p + sizeof(*eth));
            handle_request_rx(idx, body, t_rx);
            break;
        }
        case PTP_MSG_DELAY_RESP: {
            if (m->data_len < sizeof(*eth) + PTP_DELAY_RESP_LEN) {
                g_state[idx].bad_size++;
                break;
            }
            const struct ptp_msg_delay_resp *body =
                (const struct ptp_msg_delay_resp *)(p + sizeof(*eth));
            handle_response_rx(idx, body);
            break;
        }
        default:
            g_state[idx].bad_msg_type++;
            break;
        }
        rte_pktmbuf_free(m);
    }
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                       */
/* ------------------------------------------------------------------ */

static uint16_t g_ptp_ports[PTP_NUM_FLOWS];
static uint16_t g_ptp_port_count = 0;

static void *ptp_thread_fn(void *arg)
{
    (void)arg;
    g_ptp_running = true;

    uint64_t next_stats_log_mono = now_mono_ns() + 5000000000ULL;

    while (!g_ptp_stop) {
        for (uint16_t i = 0; i < g_ptp_port_count; i++)
            rx_one_port(g_ptp_ports[i]);

        uint64_t mono = now_mono_ns();

        /* M1 Master: emit Sync at 1 Hz per flow. T1 = our TX time
         * (CLOCK_REALTIME); slave subtracts its RX time T2 to estimate the
         * one-way path. */
        for (int k = 0; k < PTP_NUM_FLOWS; k++) {
            const struct ptp_flow *f = &ptp_flows[k];
            struct ptp_state *st = &g_state[k];
            if (f->role != PTP_ROLE_M1_MASTER) continue;
            if (st->last_sync_tx_mono_ns &&
                mono - st->last_sync_tx_mono_ns < 1000000000ULL) continue;

            uint64_t t1 = now_real_ns();
            uint8_t  buf[sizeof(struct ptp_eth_frame) + PTP_SYNC_PADDED_LEN];
            build_sync(buf, f, ++st->tx_seq, t1);
            if (ptp_tx_frame(f->dpdk_port, buf, sizeof(buf)) == 0) {
                st->sync_tx++;
                st->last_sync_tx_mono_ns = mono;
            } else {
                st->tx_fail++;
            }
        }

        /* Watchdog: any outstanding Delay_Req older than 1 s without a
         * matching Delay_Resp counts as a timeout and is cleared. */
        for (int k = 0; k < PTP_NUM_FLOWS; k++) {
            struct ptp_state *st = &g_state[k];
            if (ptp_flows[k].role == PTP_ROLE_M2_SLAVE &&
                st->req_outstanding &&
                mono - st->last_req_tx_mono_ns > 1000000000ULL) {
                st->resp_timeout++;
                st->req_outstanding = false;
            }
            if (st->last_sync_rx_mono_ns &&
                mono - st->last_sync_rx_mono_ns > 3000000000ULL)
                st->sync_stale = true;
        }

        /* Every 5 s, dump NIC-level stats for any PTP port that has not
         * yet seen a single Sync. Lets us tell "switch isn't sending us
         * anything" from "frames arrive but get misrouted". */
        if (mono >= next_stats_log_mono) {
            next_stats_log_mono = mono + 5000000000ULL;
            for (uint16_t i = 0; i < g_ptp_port_count; i++) {
                uint16_t port = g_ptp_ports[i];
                bool any_sync = false;
                for (int k = 0; k < PTP_NUM_FLOWS; k++)
                    if (ptp_flows[k].dpdk_port == port &&
                        g_state[k].sync_rx > 0) { any_sync = true; break; }
                if (any_sync) continue;

                struct rte_eth_stats es;
                if (rte_eth_stats_get(port, &es) == 0) {
                    printf("[PTP-dbg] silent port=%u ipkts=%lu imissed=%lu "
                           "ierr=%lu rx_nombuf=%lu opkts=%lu\n",
                           (unsigned)port,
                           (unsigned long)es.ipackets,
                           (unsigned long)es.imissed,
                           (unsigned long)es.ierrors,
                           (unsigned long)es.rx_nombuf,
                           (unsigned long)es.opackets);
                }
            }
        }

        /* Light pacing — PTP is 1 Hz traffic, no need to spin. */
        struct timespec ts = { 0, 200000 };  /* 0.2 ms */
        nanosleep(&ts, NULL);
    }

    g_ptp_running = false;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* rte_flow rule install                                               */
/* ------------------------------------------------------------------ */

int ptp_flow_rules_install(uint16_t port_id)
{
    if (!ptp_port_has_flow(port_id)) return 0;

    struct rte_flow_error err = {0};
    struct rte_flow_attr attr = {0};
    struct rte_flow_item pattern[4];
    struct rte_flow_action action[2];
    struct rte_flow_item_eth   eth_spec, eth_mask;
    struct rte_flow_item_vlan  vlan_spec, vlan_mask;
    struct rte_flow_action_queue qa;

    attr.ingress  = 1;
    attr.priority = 0;            /* Higher priority than PRBS rules (1). */

    memset(&eth_spec,  0, sizeof(eth_spec));
    memset(&eth_mask,  0, sizeof(eth_mask));
    memset(&vlan_spec, 0, sizeof(vlan_spec));
    memset(&vlan_mask, 0, sizeof(vlan_mask));
    memset(pattern,    0, sizeof(pattern));
    memset(action,     0, sizeof(action));

    /* Match: ETH (any) → VLAN (any, inner_type=0x88F7) → END. */
    vlan_spec.inner_type = rte_cpu_to_be_16(PTP_ETHERTYPE);
    vlan_mask.inner_type = rte_cpu_to_be_16(0xFFFF);

    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[0].spec = &eth_spec;
    pattern[0].mask = &eth_mask;

    pattern[1].type = RTE_FLOW_ITEM_TYPE_VLAN;
    pattern[1].spec = &vlan_spec;
    pattern[1].mask = &vlan_mask;

    pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

    qa.index = PTP_RX_QUEUE_ID;
    action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    action[0].conf = &qa;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    int ret = rte_flow_validate(port_id, &attr, pattern, action, &err);
    if (ret != 0) {
        printf("PTP Flow: Port %u validate failed: %s\n",
               port_id, err.message ? err.message : "unknown");
        return -1;
    }

    struct rte_flow *flow = rte_flow_create(port_id, &attr, pattern, action, &err);
    if (!flow) {
        printf("PTP Flow: Port %u create failed: %s\n",
               port_id, err.message ? err.message : "unknown");
        return -1;
    }

    printf("PTP Flow: Port %u EtherType=0x88F7 -> Queue %u\n",
           port_id, (unsigned)PTP_RX_QUEUE_ID);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Init / stop / dashboard                                             */
/* ------------------------------------------------------------------ */

int ptp_init(void)
{
    memset(g_state, 0, sizeof(g_state));
    memset(g_dbg_first_sync_done, 0, sizeof(g_dbg_first_sync_done));
    for (int i = 0; i < PTP_NUM_FLOWS; i++)
        g_dbg_unmatched_left[i] = PTP_DBG_UNMATCHED_BUDGET;

    /* Build unique-port list (preserving order). */
    g_ptp_port_count = 0;
    for (int i = 0; i < PTP_NUM_FLOWS; i++) {
        uint16_t p = ptp_flows[i].dpdk_port;
        bool seen = false;
        for (uint16_t k = 0; k < g_ptp_port_count; k++)
            if (g_ptp_ports[k] == p) { seen = true; break; }
        if (!seen) g_ptp_ports[g_ptp_port_count++] = p;
    }

    /* Create the PTP mempool on the socket of the first PTP port. */
    int socket_id = rte_eth_dev_socket_id(g_ptp_ports[0]);
    if (socket_id < 0) socket_id = 0;

    g_ptp_mempool = rte_pktmbuf_pool_create(
        "ptp_pool", 4096, 256, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
        (unsigned)socket_id);
    if (!g_ptp_mempool) {
        printf("PTP: failed to create mempool\n");
        return -1;
    }

    g_ptp_stop = false;
    int rc = pthread_create(&g_ptp_thread, NULL, ptp_thread_fn, NULL);
    if (rc != 0) {
        printf("PTP: pthread_create failed: %d\n", rc);
        return -1;
    }
    pthread_setname_np(g_ptp_thread, "ptp-worker");

    printf("PTP: %u flows on %u port(s), RXQ=%u TXQ=%u\n",
           (unsigned)PTP_NUM_FLOWS, (unsigned)g_ptp_port_count,
           (unsigned)PTP_RX_QUEUE_ID, (unsigned)PTP_TX_QUEUE_ID);
    return 0;
}

void ptp_stop(void)
{
    if (!g_ptp_running && !g_ptp_thread) return;
    g_ptp_stop = true;
    pthread_join(g_ptp_thread, NULL);
    g_ptp_thread = 0;
}

void ptp_print_dashboard(void)
{
    printf("\n[PTP] role-aware 1588v2  (M1=Master, M2=Slave)\n");
    printf("┌────┬──────┬────┬────┬──┬────────────────┬────────┬────────┬────────┬────────┬─────────┬────────────┬────────────┬────────────┬────────────┐\n");
    printf("│ #  │ Lbl  │ Pt │Role│NE│ VL-IDs S/Rq/Rs │ Sync   │ ReqRx  │ RspTx  │ ReqTx  │ RspRx/TO│ T2-T1 (us) │ T4-T3 (us) │ offset(us) │ delay (us) │\n");
    printf("├────┼──────┼────┼────┼──┼────────────────┼────────┼────────┼────────┼────────┼─────────┼────────────┼────────────┼────────────┼────────────┤\n");
    for (int i = 0; i < PTP_NUM_FLOWS; i++) {
        const struct ptp_flow *f = &ptp_flows[i];
        const struct ptp_state *s = &g_state[i];
        /* Role tag is always "M<n>" — M1=Master, M2=Slave. */
        char ne_c   = (f->ne == PTP_NE_A) ? 'A' : 'B';

        /* Δ_sync = T2 - T1   (master → us)
         * Δ_req  = T4 - T3   (us → master, or M1's RX of Unit's Delay_Req)
         * offset = ((T2-T1) - (T4-T3)) / 2    (E2E PTP slave; cancels clock skew)
         * delay  = ((T2-T1) + (T4-T3)) / 2    (mean path delay; clock-skew-free)
         *
         * For M1 (Master), the two legs are measured against DIFFERENT peers
         * (Sync from ATE, Delay_Req from Unit), so offset/delay are mixed-peer
         * curiosities; we still print them for cross-check. */
        double dt_sync = 0.0, dt_req = 0.0, offs = 0.0, delay = 0.0;
        bool have_sync = (s->last_t1_ns && s->last_t2_ns);
        bool have_req  = (s->last_t3_ns && s->last_t4_ns);
        if (have_sync)
            dt_sync = ((double)s->last_t2_ns - (double)s->last_t1_ns) / 1000.0;
        if (have_req)
            dt_req  = ((double)s->last_t4_ns - (double)s->last_t3_ns) / 1000.0;
        if (have_sync && have_req) {
            offs  = (dt_sync - dt_req) / 2.0;
            delay = (dt_sync + dt_req) / 2.0;
        }

        char req_rx_s[12], req_tx_s[12], resp_tx_s[12], resp_rx_s[20];
        if (f->role == PTP_ROLE_M1_MASTER) {
            snprintf(req_rx_s,  sizeof(req_rx_s),  "%lu", (unsigned long)s->req_rx);
            snprintf(resp_tx_s, sizeof(resp_tx_s), "%lu", (unsigned long)s->resp_tx);
            snprintf(req_tx_s,  sizeof(req_tx_s),  "%s", "-");
            snprintf(resp_rx_s, sizeof(resp_rx_s), "%s", "-");
        } else {
            snprintf(req_rx_s,  sizeof(req_rx_s),  "%s", "-");
            snprintf(resp_tx_s, sizeof(resp_tx_s), "%s", "-");
            snprintf(req_tx_s,  sizeof(req_tx_s),  "%lu", (unsigned long)s->req_tx);
            snprintf(resp_rx_s, sizeof(resp_rx_s), "%lu/%lu",
                     (unsigned long)s->resp_rx, (unsigned long)s->resp_timeout);
        }

        char dt_sync_s[16], dt_req_s[16], offs_s[16], delay_s[16];
        if (have_sync) snprintf(dt_sync_s, sizeof(dt_sync_s), "%+10.3f", dt_sync);
        else           snprintf(dt_sync_s, sizeof(dt_sync_s), "%10s", "-");
        if (have_req)  snprintf(dt_req_s,  sizeof(dt_req_s),  "%+10.3f", dt_req);
        else           snprintf(dt_req_s,  sizeof(dt_req_s),  "%10s", "-");
        if (have_sync && have_req) {
            snprintf(offs_s,  sizeof(offs_s),  "%+10.3f", offs);
            snprintf(delay_s, sizeof(delay_s), "%+10.3f", delay);
        } else {
            snprintf(offs_s,  sizeof(offs_s),  "%10s", "-");
            snprintf(delay_s, sizeof(delay_s), "%10s", "-");
        }

        /* SyncRx column shows TX count for M1 (master emits Sync), RX count
         * for M2 (slave receives Sync). */
        uint64_t sync_count = (f->role == PTP_ROLE_M1_MASTER) ? s->sync_tx
                                                              : s->sync_rx;
        printf("│ %2d │ %-4s │ %2u │ M%c │%c │ %5u/%4u/%4u │ %6lu │ %6s │ %6s │ %6s │ %7s │ %s │ %s │ %s │ %s │\n",
               i, f->j_label, (unsigned)f->dpdk_port,
               (f->role == PTP_ROLE_M1_MASTER) ? '1' : '2',
               ne_c,
               f->sync_vl_id, f->req_vl_id, f->resp_vl_id,
               (unsigned long)sync_count,
               req_rx_s, resp_tx_s,
               req_tx_s, resp_rx_s,
               dt_sync_s, dt_req_s, offs_s, delay_s);
    }
    printf("└────┴──────┴────┴────┴──┴────────────────┴────────┴────────┴────────┴────────┴─────────┴────────────┴────────────┴────────────┴────────────┘\n");

    /* Bad counters + stale flags */
    uint64_t bv=0, bn=0, bs=0, bt=0, af=0, tf=0;
    int stale_a=0, stale_b=0;
    for (int i = 0; i < PTP_NUM_FLOWS; i++) {
        bv += g_state[i].bad_vlan;
        bn += g_state[i].bad_ne;
        bs += g_state[i].bad_size;
        bt += g_state[i].bad_msg_type;
        af += g_state[i].alloc_fail;
        tf += g_state[i].tx_fail;
        if (g_state[i].sync_stale) {
            if (ptp_flows[i].ne == PTP_NE_A) stale_a = 1;
            else stale_b = 1;
        }
    }
    printf("[PTP] bad vlan=%lu ne=%lu size=%lu type=%lu | alloc_fail=%lu tx_fail=%lu | stale: A=%s B=%s\n",
           (unsigned long)bv, (unsigned long)bn, (unsigned long)bs,
           (unsigned long)bt, (unsigned long)af, (unsigned long)tf,
           stale_a ? "yes" : "no", stale_b ? "yes" : "no");
}
