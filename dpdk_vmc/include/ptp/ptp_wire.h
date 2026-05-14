#ifndef PTP_WIRE_H
#define PTP_WIRE_H

#include <stdint.h>
#include <string.h>
#include <rte_byteorder.h>

/* Proprietary PTP-like protocol over raw Ethernet — NOT IEEE 1588 PTPv2.
 * The peer slave/master uses a fixed 120-byte (vlan-untagged) frame whose
 * structure is described in the deployment spec:
 *
 *   offset (no vlan)  size  field
 *   0..5              6     dst MAC = 03 00 00 00 || VL-ID (BE)
 *   6..11             6     src MAC = 02 00 00 00 00 || NE
 *   12..13            2     EtherType = 0x88F7
 *   14                1     PTP_MSG_TYPE: 0x00 Sync / 0x01 Req / 0x09 Resp
 *   15..49            35    PTP_DATA   : fixed byte blob (see PTP_DATA_BLOB)
 *   50..57            8     PTP_TIME_1 : 4B sec BE + 4B ns BE (T1 / T3)
 *   58                1     filler 0x00
 *   59..66            8     PTP_TIME_2 : 4B sec BE + 4B ns BE (T4 for Resp)
 *   67..119           53    zero padding
 *
 * With our inline 802.1Q tag the wire frame becomes 124 bytes; every offset
 * after the eth+vlan prefix (18B) is shifted by 4 relative to the spec. */

#define PTP_ETHERTYPE          0x88F7

#define PTP_MSG_SYNC           0x00
#define PTP_MSG_DELAY_REQ      0x01
#define PTP_MSG_DELAY_RESP     0x09

#define PTP_VLAN_TPID          0x8100

/* Wire totals */
#define PTP_BODY_LEN           106                  /* MSG_TYPE..pad, vlan haric */
#define PTP_FRAME_NOVLAN_LEN   120                  /* eth + 88F7 + body */
#define PTP_FRAME_VLAN_LEN     124                  /* +4 byte 802.1Q */

/* PTP_DATA blobs (35 bytes each), strict-checked by the peer side. Wire
 * captures show that master and slave use distinct blobs — they differ in
 * the domain byte, the ASCII identifier, and the trailing 5 bytes. The
 * receiving side appears to verify the blob byte-for-byte, so a Sync that
 * carries the slave blob (or vice versa) gets dropped silently. */
#define PTP_DATA_LEN           35
static const uint8_t PTP_DATA_MASTER_BLOB[PTP_DATA_LEN] = {
    0x02, 0x00, 0x6a, 0x0b, 0x00, 0x01, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x6d, 0x61, 0x73, 0x74, 0x65,
    0x72, 0x64, 0x65, 0x76, 0x2e, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00
};
static const uint8_t PTP_DATA_SLAVE_BLOB[PTP_DATA_LEN] = {
    0x02, 0x00, 0x6a, 0x0a, 0x00, 0x01, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x73, 0x6c, 0x61, 0x76, 0x65,
    0x5f, 0x64, 0x65, 0x76, 0x2e, 0x00, 0x01, 0x01,
    0xff, 0x12, 0x13
};

/* Offsets within the PTP body (i.e. after eth + 802.1Q + 88F7 prefix). */
#define PTP_BODY_MSG_TYPE_OFF  0
#define PTP_BODY_DATA_OFF      1
#define PTP_BODY_TIME1_OFF     (PTP_BODY_DATA_OFF + PTP_DATA_LEN)        /* 36 */
#define PTP_BODY_FILLER_OFF    (PTP_BODY_TIME1_OFF + 8)                  /* 44 */
#define PTP_BODY_TIME2_OFF     (PTP_BODY_FILLER_OFF + 1)                 /* 45 */
#define PTP_BODY_PAD_OFF       (PTP_BODY_TIME2_OFF + 8)                  /* 53 */

/* Ethernet + inline 802.1Q + EtherType prefix. Total 18 bytes; PTP body
 * starts immediately after. */
struct ptp_eth_frame {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t vlan_tpid_be;        /* 0x8100 */
    uint16_t vlan_tci_be;         /* (priority << 13) | vlan_id */
    uint16_t ether_type_be;       /* 0x88F7 */
} __attribute__((packed));

/* Encode/decode the 8-byte proprietary timestamp (4B sec BE + 4B ns BE).
 * Note: this is NOT the PTPv2 10-byte timestamp (6B sec + 4B ns). */
static inline void ptp_write_time(uint8_t *dst, uint64_t ns)
{
    uint32_t sec = (uint32_t)(ns / 1000000000ULL);
    uint32_t rem = (uint32_t)(ns % 1000000000ULL);
    uint32_t sec_be = rte_cpu_to_be_32(sec);
    uint32_t ns_be  = rte_cpu_to_be_32(rem);
    memcpy(dst,     &sec_be, 4);
    memcpy(dst + 4, &ns_be,  4);
}

static inline uint64_t ptp_read_time(const uint8_t *src)
{
    uint32_t sec_be, ns_be;
    memcpy(&sec_be, src,     4);
    memcpy(&ns_be,  src + 4, 4);
    uint64_t sec = rte_be_to_cpu_32(sec_be);
    uint32_t ns  = rte_be_to_cpu_32(ns_be);
    return sec * 1000000000ULL + ns;
}

#endif /* PTP_WIRE_H */
