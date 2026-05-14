/*
 * Dual Proprietary-PTP Sync TX (raw socket, sendmmsg)
 *
 * dpdk_vmc tarafindaki M1 master Sync akisini (ptp_flows[0] ve [1])
 * bagimsiz uretmek icin standalone test araci. Her saniyede 2 paket gonderir:
 *   - NE_A: VLAN 100, VL-ID 1500, src MAC 02:00:00:00:00:20
 *   - NE_B: VLAN 97,  VL-ID 1501, src MAC 02:00:00:00:00:40
 *
 * Frame layout — sahanin proprietary spec'i (124 byte, vlan'li):
 *   eth(14) + 802.1Q(4) + MSG_TYPE(1) + PTP_DATA(35) + PTP_TIME_1(8)
 *   + filler(1) + PTP_TIME_2(8) + pad(53) = 124
 *
 * PTP_DATA sabit blob, PTP_TIME alanlari 4B sec BE + 4B ns BE (PTPv2'nin
 * 10B timestamp formati DEGIL).
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

/* M1 master flow tablosundan (dpdk_vmc/src/ptp/ptp.c) */
#define NE_A               0x20
#define NE_B               0x40

#define VLAN_ID_A          100
#define VL_ID_A            1500
#define SRC_MAC_A          { 0x02, 0x00, 0x00, 0x00, 0x00, NE_A }

#define VLAN_ID_B          97
#define VL_ID_B            1501
#define SRC_MAC_B          { 0x02, 0x00, 0x00, 0x00, 0x00, NE_B }

/* dst MAC = 03:00:00:00 || VL-ID (BE) */
#define DST_MAC_PREFIX     { 0x03, 0x00, 0x00, 0x00 }

/* Proprietary PTP MSG_TYPE */
#define PTP_MSG_SYNC       0x00
#define PTP_MSG_REQUEST    0x01
#define PTP_MSG_RESPONSE   0x09

/* Frame offsets (vlan-tagged 124 byte total) */
#define ETH_HDR_LEN          14
#define VLAN_HDR_LEN         4
#define PTP_BODY_LEN         106
#define PACKET_SIZE          (ETH_HDR_LEN + VLAN_HDR_LEN + PTP_BODY_LEN)  /* 124 */

#define BODY_MSG_TYPE_OFF    0
#define BODY_DATA_OFF        1
#define PTP_DATA_LEN         35
#define BODY_TIME1_OFF       (BODY_DATA_OFF + PTP_DATA_LEN)               /* 36 */
#define BODY_FILLER_OFF      (BODY_TIME1_OFF + 8)                         /* 44 */
#define BODY_TIME2_OFF       (BODY_FILLER_OFF + 1)                        /* 45 */

/* PTP_DATA blob'lari — master ve slave farkli 35-byte sabit blob kullaniyor.
 * Wire capture'lara gore karsi taraf bu byte'lari strict check ediyor; Sync
 * gonderirken master blob, Delay_Req gonderirken slave blob lazim. */
static const uint8_t PTP_DATA_MASTER_BLOB[PTP_DATA_LEN] = {
    0x02, 0x00, 0x6a, 0x0b, 0x00, 0x01, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x6d, 0x61, 0x73, 0x74, 0x65,
    0x72, 0x64, 0x65, 0x76, 0x2e, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x00
};
static const uint8_t PTP_DATA_SLAVE_BLOB[PTP_DATA_LEN] __attribute__((unused)) = {
    0x02, 0x00, 0x6a, 0x0a, 0x00, 0x01, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x73, 0x6c, 0x61, 0x76, 0x65,
    0x5f, 0x64, 0x65, 0x76, 0x2e, 0x00, 0x01, 0x01,
    0xff, 0x12, 0x13
};

/* ========================================== */
/* TIMESTAMP                                   */
/* ========================================== */

static void write_ptp_time(uint8_t *dst, uint64_t ns)
{
    uint32_t sec = (uint32_t)(ns / 1000000000ULL);
    uint32_t rem = (uint32_t)(ns % 1000000000ULL);
    uint32_t sec_be = htonl(sec);
    uint32_t ns_be  = htonl(rem);
    memcpy(dst,     &sec_be, 4);
    memcpy(dst + 4, &ns_be,  4);
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
                         uint16_t vlan_id,
                         uint16_t vl_id,
                         const uint8_t *src_mac,
                         uint8_t msg_type,
                         const uint8_t *data_blob,
                         uint64_t time1_ns,
                         uint64_t time2_ns)
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

    /* --- Proprietary PTP body (106 B), eth+vlan sonrasi offset 18 --- */
    uint8_t *body = pkt + 18;
    body[BODY_MSG_TYPE_OFF] = msg_type;
    memcpy(body + BODY_DATA_OFF, data_blob, PTP_DATA_LEN);
    write_ptp_time(body + BODY_TIME1_OFF, time1_ns);
    /* BODY_FILLER_OFF zaten 0x00 (memset) */
    write_ptp_time(body + BODY_TIME2_OFF, time2_ns);
    /* Geri kalan 53 byte pad zaten 0x00 */
}

/* ========================================== */
/* TRACE                                       */
/* ========================================== */

static void trace_packet(const uint8_t *pkt, uint64_t t1_ns, const char *label)
{
    uint16_t tci = ((uint16_t)pkt[14] << 8) | pkt[15];
    uint16_t vlan_id = tci & 0x0FFF;
    uint16_t vl_id   = ((uint16_t)pkt[4] << 8) | pkt[5];

    printf("[%s] VLAN=%-3u VL-ID=%-4u T1=%" PRIu64
           "  DA=%02x:%02x:%02x:%02x:%02x:%02x  SA=%02x:%02x:%02x:%02x:%02x:%02x  len=%d\n",
           label, vlan_id, vl_id, t1_ns,
           pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5],
           pkt[6], pkt[7], pkt[8], pkt[9], pkt[10], pkt[11],
           PACKET_SIZE);
}

/* ========================================== */
/* MAIN                                        */
/* ========================================== */

int main(void)
{
    printf("=== Dual Proprietary-PTP Sync TX ===\n");
    printf("Interface: %s\n", INTERFACE);
    printf("NE_A: VLAN %u, VL-ID %u\n", VLAN_ID_A, VL_ID_A);
    printf("NE_B: VLAN %u, VL-ID %u\n", VLAN_ID_B, VL_ID_B);
    printf("Frame size: %d byte (vlan'li)\n", PACKET_SIZE);
    printf("====================================\n\n");

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

    printf("Gonderim basliyor... (Ctrl+C ile durdur)\n\n");

    while (1) {
        uint64_t t1 = now_real_ns();
        build_packet(pkt_a, VLAN_ID_A, VL_ID_A, src_mac_a, PTP_MSG_SYNC,
                     PTP_DATA_MASTER_BLOB, t1, 0);
        build_packet(pkt_b, VLAN_ID_B, VL_ID_B, src_mac_b, PTP_MSG_SYNC,
                     PTP_DATA_MASTER_BLOB, t1, 0);

        trace_packet(pkt_a, t1, "TX-A");
        trace_packet(pkt_b, t1, "TX-B");

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
            printf("  -> %d Sync paketi gonderildi (T1=%" PRIu64 ")\n\n", n, t1);
        }

        sleep(TX_INTERVAL_SEC);
    }

    close(sock);
    return 0;
}
