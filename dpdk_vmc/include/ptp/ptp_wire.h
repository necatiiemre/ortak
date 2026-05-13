#ifndef PTP_WIRE_H
#define PTP_WIRE_H

#include <stdint.h>
#include <string.h>
#include <rte_byteorder.h>

/* PTPv2 over IEEE 802.3 / Ethernet (IEEE 1588-2008) */
#define PTP_ETHERTYPE          0x88F7
#define PTP_VERSION            0x02

/* messageType (low 4 bits of byte 0). transportSpecific in high 4 bits. */
#define PTP_MSG_SYNC           0x00
#define PTP_MSG_DELAY_REQ      0x01
#define PTP_MSG_DELAY_RESP     0x09

/* PTPv2 common header is 34 bytes. Sync / Delay_Req add a 10-byte
 * originTimestamp. Delay_Resp adds 10-byte receiveTimestamp + 10-byte
 * requestingPortIdentity. */
#define PTP_HDR_LEN            34
#define PTP_SYNC_LEN           (PTP_HDR_LEN + 10)               /* 44 */
#define PTP_DELAY_REQ_LEN      (PTP_HDR_LEN + 10)               /* 44 */
#define PTP_DELAY_RESP_LEN     (PTP_HDR_LEN + 10 + 10)          /* 54 */

/* The peer master emits Sync with messageLength=106 (header + 10B
 * originTimestamp + 62B zero padding) on a 124-byte wire frame. Mirror this
 * exact size on TX so M1 slaves that strict-check the frame shape accept
 * our Sync. Delay_Req / Delay_Resp keep the spec-defined sizes — those
 * are already accepted by the peer. */
#define PTP_SYNC_PADDED_LEN    106

/* PTPv2 timestamp = 6-byte seconds (big-endian) + 4-byte nanoseconds (BE) */
struct ptp_timestamp {
    uint8_t  sec_hi;       /* seconds[47..40] */
    uint8_t  sec[5];       /* seconds[39..0]  (combined 48-bit BE) */
    uint32_t ns_be;        /* nanoseconds, BE */
} __attribute__((packed));

/* PTPv2 common message header (34 bytes). */
struct ptp_header {
    uint8_t  ts_msg;            /* high4=transportSpecific, low4=messageType */
    uint8_t  ver;               /* reserved(4) | versionPTP(4) */
    uint16_t length_be;
    uint8_t  domain;
    uint8_t  reserved0;
    uint16_t flags_be;
    uint64_t correction_be;     /* 8B correctionField (ns << 16) */
    uint32_t reserved1;
    uint8_t  source_port_id[10]; /* 8B clockIdentity + 2B portNumber */
    uint16_t sequence_id_be;
    uint8_t  control;
    int8_t   log_msg_interval;
} __attribute__((packed));

/* Sync / Delay_Req payload after header */
struct ptp_msg_sync {
    struct ptp_header     hdr;
    struct ptp_timestamp  origin_ts;          /* 10B */
} __attribute__((packed));

/* Delay_Resp payload after header */
struct ptp_msg_delay_resp {
    struct ptp_header     hdr;
    struct ptp_timestamp  receive_ts;         /* T4 (master's RX time of Delay_Req) */
    uint8_t               requesting_port_id[10];
} __attribute__((packed));

/* Ethernet + inline 802.1Q + PTP frame. VLAN is written into the packet
 * bytes by build_eth_ptp (no HW offload), mirroring the PRBS path so we are
 * not dependent on RTE_ETH_TX_OFFLOAD_VLAN_INSERT being negotiated. */
#define PTP_VLAN_TPID          0x8100
struct ptp_eth_frame {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t vlan_tpid_be;         /* 0x8100 */
    uint16_t vlan_tci_be;          /* (priority << 13) | vlan_id */
    uint16_t ether_type_be;        /* inner = 0x88F7 */
    /* PTP payload follows */
} __attribute__((packed));

/* Helpers */
static inline uint64_t ptp_ts_to_ns(const struct ptp_timestamp *t)
{
    uint64_t sec = ((uint64_t)t->sec_hi << 40) |
                   ((uint64_t)t->sec[0] << 32) |
                   ((uint64_t)t->sec[1] << 24) |
                   ((uint64_t)t->sec[2] << 16) |
                   ((uint64_t)t->sec[3] <<  8) |
                   ((uint64_t)t->sec[4]);
    uint32_t ns = rte_be_to_cpu_32(t->ns_be);
    return sec * 1000000000ULL + ns;
}

static inline void ptp_ns_to_ts(uint64_t ns, struct ptp_timestamp *t)
{
    uint64_t sec = ns / 1000000000ULL;
    uint32_t rem = (uint32_t)(ns % 1000000000ULL);
    t->sec_hi = (uint8_t)((sec >> 40) & 0xFF);
    t->sec[0] = (uint8_t)((sec >> 32) & 0xFF);
    t->sec[1] = (uint8_t)((sec >> 24) & 0xFF);
    t->sec[2] = (uint8_t)((sec >> 16) & 0xFF);
    t->sec[3] = (uint8_t)((sec >>  8) & 0xFF);
    t->sec[4] = (uint8_t)( sec        & 0xFF);
    t->ns_be  = rte_cpu_to_be_32(rem);
}

#endif /* PTP_WIRE_H */
