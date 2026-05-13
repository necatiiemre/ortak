/*
 * Dual PTP Sync TX (raw socket, sendmmsg)
 *
 * dpdk_vmc tarafindaki M1 master akisini (ptp_flows[0] ve [1]) bagimsiz
 * uretmek icin standalone test araci. Her saniyede 2 paket gonderir:
 *   - NE_A: VLAN 100, VL-ID 1500, src MAC 02:00:00:00:00:20
 *   - NE_B: VLAN 97,  VL-ID 1501, src MAC 02:00:00:00:00:40
 *
 * Frame layout (FCS haric, toplam 62 B):
 *   eth(14) + 802.1Q(4) + PTPv2 Sync(44)
 *
 * Derleme:   make
 * Calistir:  sudo ./single_packet_tx
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_ether.h>

/* ========================================== */
/* YAPILANDIRMA                                */
/* ========================================== */

#define INTERFACE          "eno1"        /* TX cikis bacagi */
#define TX_INTERVAL_SEC    1             /* PTP nominal 1 Hz */

#define VLAN_PRIORITY      0
#define PTP_ETHERTYPE      0x88F7

/* M1 master flow tablosundan (dpdk_vmc/src/ptp/ptp.c:42-43) */
#define NE_A               0x20
#define NE_B               0x40

#define VLAN_ID_A          100
#define VL_ID_A            1500
#define SRC_MAC_A          { 0x02, 0x00, 0x00, 0x00, 0x00, NE_A }

#define VLAN_ID_B          97
#define VL_ID_B            1501
#define SRC_MAC_B          { 0x02, 0x00, 0x00, 0x00, 0x00, NE_B }

/* dst MAC = 03:00:00:00 || VL-ID (BE) — build_eth_ptp ile ayni kural */
#define DST_MAC_PREFIX     { 0x03, 0x00, 0x00, 0x00 }

/* PTPv2 ortak header + Sync gövde sabitleri (ptp_wire.h ile ayni) */
#define PTP_VERSION        0x02
#define PTP_MSG_SYNC       0x00
#define PTP_HDR_LEN        34
#define PTP_SYNC_LEN       (PTP_HDR_LEN + 10)   /* 44 */
#define PTP_DOMAIN         10
#define PTP_FLAGS          0x0102               /* alternateMaster | leap59 — peer master uyumu */
#define PTP_CONTROL_SYNC   0x00
#define PTP_LOG_MSG_INT    0x00                 /* 1 Hz Sync → 2^0 = 1 s */

#define ETH_HDR_LEN        14
#define VLAN_HDR_LEN       4
#define PACKET_SIZE        (ETH_HDR_LEN + VLAN_HDR_LEN + PTP_SYNC_LEN)  /* 62 */

/* ========================================== */
/* PTP TIMESTAMP                               */
/* ========================================== */

/* 48-bit BE seconds + 32-bit BE nanoseconds, ptp_ns_to_ts ile ayni encoding */
static void ptp_ns_to_ts(uint64_t ns, uint8_t out[10])
{
    uint64_t sec = ns / 1000000000ULL;
    uint32_t rem = (uint32_t)(ns % 1000000000ULL);
    out[0] = (uint8_t)((sec >> 40) & 0xFF);
    out[1] = (uint8_t)((sec >> 32) & 0xFF);
    out[2] = (uint8_t)((sec >> 24) & 0xFF);
    out[3] = (uint8_t)((sec >> 16) & 0xFF);
    out[4] = (uint8_t)((sec >>  8) & 0xFF);
    out[5] = (uint8_t)( sec        & 0xFF);
    uint32_t rem_be = htonl(rem);
    memcpy(out + 6, &rem_be, 4);
}

static uint64_t now_real_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ========================================== */
/* PAKET INSA                                  */
/* ========================================== */

static void build_packet(uint8_t *pkt,
                         uint16_t sequence_id,
                         uint16_t vlan_id,
                         uint16_t vl_id,
                         const uint8_t *src_mac,
                         uint8_t ne,
                         uint64_t t1_ns)
{
    memset(pkt, 0, PACKET_SIZE);

    /* --- Ethernet (14 B) --- */
    uint8_t dst_mac_prefix[] = DST_MAC_PREFIX;
    memcpy(pkt + 0, dst_mac_prefix, 4);
    pkt[4] = (vl_id >> 8) & 0xFF;
    pkt[5] =  vl_id       & 0xFF;
    memcpy(pkt + 6, src_mac, 6);
    pkt[12] = 0x81;  /* 802.1Q TPID */
    pkt[13] = 0x00;

    /* --- 802.1Q (4 B) --- */
    uint16_t tci = ((VLAN_PRIORITY & 0x7) << 13) | (vlan_id & 0x0FFF);
    pkt[14] = (tci >> 8) & 0xFF;
    pkt[15] =  tci       & 0xFF;
    pkt[16] = (PTP_ETHERTYPE >> 8) & 0xFF;
    pkt[17] =  PTP_ETHERTYPE       & 0xFF;

    /* --- PTPv2 ortak header (34 B) --- */
    uint8_t *p = pkt + 18;
    p[0]  = PTP_MSG_SYNC & 0x0F;          /* transportSpecific=0 | msgType=Sync */
    p[1]  = PTP_VERSION;                  /* reserved=0 | ver=2 */
    p[2]  = (PTP_SYNC_LEN >> 8) & 0xFF;   /* messageLength BE */
    p[3]  =  PTP_SYNC_LEN       & 0xFF;
    p[4]  = PTP_DOMAIN;
    p[5]  = 0;                            /* reserved */
    p[6]  = (PTP_FLAGS >> 8) & 0xFF;      /* flagField BE */
    p[7]  =  PTP_FLAGS       & 0xFF;
    /* p[8..15]  correctionField (8B) = 0 */
    /* p[16..19] reserved (4B)         = 0 */
    /* sourcePortIdentity (10B): clockIdentity(8) + portNumber(2) */
    p[20] = 0x02;
    p[27] = ne;
    p[28] = 0x00;
    p[29] = 0x01;
    p[30] = (sequence_id >> 8) & 0xFF;    /* sequenceId BE */
    p[31] =  sequence_id       & 0xFF;
    p[32] = PTP_CONTROL_SYNC;
    p[33] = PTP_LOG_MSG_INT;

    /* --- Sync gövdesi: originTimestamp (10 B) --- */
    ptp_ns_to_ts(t1_ns, p + 34);
}

/* ========================================== */
/* TRACE                                       */
/* ========================================== */

static void trace_packet(const uint8_t *pkt, uint16_t seq, uint64_t t1_ns, const char *label)
{
    uint16_t tci = ((uint16_t)pkt[14] << 8) | pkt[15];
    uint16_t vlan_id = tci & 0x0FFF;
    uint16_t vl_id   = ((uint16_t)pkt[4] << 8) | pkt[5];

    printf("[%s] seq=%-5u VLAN=%-3u VL-ID=%-4u T1=%" PRIu64
           "  DA=%02x:%02x:%02x:%02x:%02x:%02x  SA=%02x:%02x:%02x:%02x:%02x:%02x\n",
           label, seq, vlan_id, vl_id, t1_ns,
           pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5],
           pkt[6], pkt[7], pkt[8], pkt[9], pkt[10], pkt[11]);
}

/* ========================================== */
/* MAIN                                        */
/* ========================================== */

int main(void)
{
    printf("=== Dual PTP Sync TX ===\n");
    printf("Interface: %s\n", INTERFACE);
    printf("NE_A: VLAN %u, VL-ID %u\n", VLAN_ID_A, VL_ID_A);
    printf("NE_B: VLAN %u, VL-ID %u\n", VLAN_ID_B, VL_ID_B);
    printf("Frame size (no FCS): %d byte\n", PACKET_SIZE);
    printf("==========================\n\n");

    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) { perror("socket"); return 1; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, INTERFACE, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(sock);
        return 1;
    }

    /* Iki ayri sockaddr — dst MAC'ler farkli (VL-ID 1500 vs 1501) */
    struct sockaddr_ll saddr_a, saddr_b;
    uint8_t dst_prefix[] = DST_MAC_PREFIX;

    memset(&saddr_a, 0, sizeof(saddr_a));
    saddr_a.sll_family   = AF_PACKET;
    saddr_a.sll_protocol = htons(ETH_P_ALL);
    saddr_a.sll_ifindex  = ifr.ifr_ifindex;
    saddr_a.sll_halen    = 6;
    memcpy(saddr_a.sll_addr, dst_prefix, 4);
    saddr_a.sll_addr[4] = (VL_ID_A >> 8) & 0xFF;
    saddr_a.sll_addr[5] =  VL_ID_A       & 0xFF;

    memcpy(&saddr_b, &saddr_a, sizeof(saddr_a));
    saddr_b.sll_addr[4] = (VL_ID_B >> 8) & 0xFF;
    saddr_b.sll_addr[5] =  VL_ID_B       & 0xFF;

    uint8_t pkt_a[PACKET_SIZE];
    uint8_t pkt_b[PACKET_SIZE];
    uint8_t src_mac_a[6] = SRC_MAC_A;
    uint8_t src_mac_b[6] = SRC_MAC_B;

    struct mmsghdr msgs[2];
    struct iovec   iovecs[2];

    uint16_t seq = 0;
    printf("Gonderim basliyor... (Ctrl+C ile durdur)\n\n");

    while (1) {
        uint64_t t1 = now_real_ns();
        build_packet(pkt_a, seq, VLAN_ID_A, VL_ID_A, src_mac_a, NE_A, t1);
        build_packet(pkt_b, seq, VLAN_ID_B, VL_ID_B, src_mac_b, NE_B, t1);

        trace_packet(pkt_a, seq, t1, "TX-A");
        trace_packet(pkt_b, seq, t1, "TX-B");

        memset(msgs, 0, sizeof(msgs));

        iovecs[0].iov_base = pkt_a;
        iovecs[0].iov_len  = PACKET_SIZE;
        msgs[0].msg_hdr.msg_name    = &saddr_a;
        msgs[0].msg_hdr.msg_namelen = sizeof(saddr_a);
        msgs[0].msg_hdr.msg_iov     = &iovecs[0];
        msgs[0].msg_hdr.msg_iovlen  = 1;

        iovecs[1].iov_base = pkt_b;
        iovecs[1].iov_len  = PACKET_SIZE;
        msgs[1].msg_hdr.msg_name    = &saddr_b;
        msgs[1].msg_hdr.msg_namelen = sizeof(saddr_b);
        msgs[1].msg_hdr.msg_iov     = &iovecs[1];
        msgs[1].msg_hdr.msg_iovlen  = 1;

        int n = sendmmsg(sock, msgs, 2, 0);
        if (n == -1) {
            perror("sendmmsg");
        } else {
            printf("  -> %d PTP Sync paketi gonderildi (seq=%u)\n\n", n, seq);
        }

        seq++;
        sleep(TX_INTERVAL_SEC);
    }

    close(sock);
    return 0;
}
