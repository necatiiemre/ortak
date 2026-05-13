/*
 * Dual Packet TX (Optimized with sendmmsg)
 * Raw socket ile ayni anda iki farkli VLAN ve MAC adresi ile paket basar.
 * * Derleme: gcc dual_tx_batch.c -o dual_tx_batch
 * Calistirma: sudo ./dual_tx_batch
 */

 #define _GNU_SOURCE // sendmmsg destegi icin zorunlu
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <stdint.h>
 #include <stdbool.h>
 #include <inttypes.h>
 #include <unistd.h>
 #include <errno.h>
 #include <arpa/inet.h>
 #include <net/if.h>
 #include <net/ethernet.h>
 #include <netpacket/packet.h>
 #include <sys/socket.h>
 #include <sys/ioctl.h>
 #include <linux/if_ether.h>
 
 // ==========================================
 // YAPILANDIRMA MAKROLARI
 // ==========================================
 
 // Ag arayuzu
 #define INTERFACE       "eno1"
 
 // VLAN ve MAC Entegrasyonlari
 #define VLAN_ID_1       97
 #define SRC_MAC_1       { 0x02, 0x00, 0x00, 0x00, 0x00, 0x20 }
 
 #define VLAN_ID_2       98
 #define SRC_MAC_2       { 0x02, 0x00, 0x00, 0x00, 0x00, 0x40 }
 
 #define VLAN_PRIORITY   0
 #define VL_ID           10001     // DST MAC son 2 byte + DST IP son 2 byte
 
 // Gonderim araligi (saniye)
 #define TX_INTERVAL_SEC 10
 
 // DST MAC: 03:00:00:00:XX:XX (son 2 byte = VL-ID big-endian)
 #define DST_MAC_PREFIX  { 0x03, 0x00, 0x00, 0x00 }
 
 // IP adresleri
 #define SRC_IP          "10.0.0.0"
 // DST IP: 224.224.X.X (son 2 byte = VL-ID big-endian, multicast)
 #define DST_IP_PREFIX   0xE0E00000  // 224.224.0.0
 
 // UDP portlari
 #define UDP_SRC_PORT    100
 #define UDP_DST_PORT    100
 
 // IP
 #define PKT_TTL         1
 
 // Paket boyutlari
 #define ETH_HDR_LEN     14
 #define VLAN_HDR_LEN    4
 #define IP_HDR_LEN      20
 #define UDP_HDR_LEN     8
 #define SEQ_BYTES       8
 #define PAYLOAD_SIZE    1467    // SEQ(8) + PRBS_DATA(1458) + DTN_SEQ(1)
 #define PACKET_SIZE     (ETH_HDR_LEN + VLAN_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN + PAYLOAD_SIZE)
 
 // PRBS-31
 #define PRBS31_POLY     0x80000001  // x^31 + x^28 + 1
 #define PRBS31_SEED     0x7F3A2B01  // Test icin seed
 
 // ==========================================
 // PRBS-31 GENERATOR
 // ==========================================
 static uint32_t prbs31_state;
 
 static void prbs31_init(uint32_t seed)
 {
     prbs31_state = seed & 0x7FFFFFFF;
     if (prbs31_state == 0) prbs31_state = 1;
 }
 
 static uint8_t prbs31_next_byte(void)
 {
     uint8_t byte = 0;
     for (int bit = 0; bit < 8; bit++) {
         int feedback = ((prbs31_state >> 30) ^ (prbs31_state >> 27)) & 1;
         prbs31_state = ((prbs31_state << 1) | feedback) & 0x7FFFFFFF;
         byte = (byte << 1) | (prbs31_state & 1);
     }
     return byte;
 }
 
 // ==========================================
 // DTN SEQUENCE
 // ==========================================
 static inline uint8_t calc_dtn_seq(uint64_t seq)
 {
     if (seq == 0) return 0;
     return (uint8_t)(((seq - 1) % 255) + 1);
 }
 
 // ==========================================
 // IP CHECKSUM
 // ==========================================
 static uint16_t ip_checksum(const void *data, int len)
 {
     const uint8_t *p = (const uint8_t *)data;
     uint32_t sum = 0;
     for (int i = 0; i < len - 1; i += 2)
         sum += (p[i] << 8) | p[i + 1];
     if (len & 1)
         sum += p[len - 1] << 8;
     while (sum >> 16)
         sum = (sum & 0xFFFF) + (sum >> 16);
     uint16_t result = ~sum & 0xFFFF;
     return htons(result);
 }
 
 // ==========================================
 // PAKET OLUSTURMA (DINAMIK VLAN VE MAC ILE)
 // ==========================================
 static void build_packet(uint8_t *pkt, uint64_t seq, uint16_t vlan_id, const uint8_t *src_mac)
 {
     memset(pkt, 0, PACKET_SIZE);
     uint16_t offset = 0;
 
     // --- Ethernet Header (14 bytes) ---
     uint8_t dst_mac[] = DST_MAC_PREFIX;
     pkt[0] = dst_mac[0]; pkt[1] = dst_mac[1];
     pkt[2] = dst_mac[2]; pkt[3] = dst_mac[3];
     pkt[4] = (VL_ID >> 8) & 0xFF;
     pkt[5] = VL_ID & 0xFF;
 
     memcpy(pkt + 6, src_mac, 6); // Dinamik SRC MAC
 
     pkt[12] = 0x81; // EtherType = VLAN
     pkt[13] = 0x00;
     offset = ETH_HDR_LEN;
 
     // --- VLAN Header (4 bytes) ---
     uint16_t tci = ((VLAN_PRIORITY & 0x7) << 13) | (vlan_id & 0x0FFF); // Dinamik VLAN ID
     pkt[offset + 0] = (tci >> 8) & 0xFF;
     pkt[offset + 1] = tci & 0xFF;
     pkt[offset + 2] = 0x08; // Inner EtherType = IPv4
     pkt[offset + 3] = 0x00;
     offset += VLAN_HDR_LEN;
 
     // --- IPv4 Header (20 bytes) ---
     uint8_t *ip = pkt + offset;
     ip[0] = 0x45;
     ip[1] = 0x00;
     uint16_t ip_total = IP_HDR_LEN + UDP_HDR_LEN + PAYLOAD_SIZE;
     ip[2] = (ip_total >> 8) & 0xFF;
     ip[3] = ip_total & 0xFF;
     ip[4] = 0x00; ip[5] = 0x00;
     ip[6] = 0x00; ip[7] = 0x00;
     ip[8] = PKT_TTL;
     ip[9] = 17; // UDP
 
     inet_pton(AF_INET, SRC_IP, ip + 12);
     uint32_t dst_ip = htonl(DST_IP_PREFIX | (VL_ID & 0xFFFF));
     memcpy(ip + 16, &dst_ip, 4);
 
     ip[10] = 0; ip[11] = 0;
     uint16_t cksum = ip_checksum(ip, IP_HDR_LEN);
     memcpy(ip + 10, &cksum, 2);
     offset += IP_HDR_LEN;
 
     // --- UDP Header (8 bytes) ---
     uint8_t *udp = pkt + offset;
     uint16_t sp = htons(UDP_SRC_PORT);
     uint16_t dp = htons(UDP_DST_PORT);
     uint16_t udp_len = htons(UDP_HDR_LEN + PAYLOAD_SIZE);
     memcpy(udp + 0, &sp, 2);
     memcpy(udp + 2, &dp, 2);
     memcpy(udp + 4, &udp_len, 2);
     udp[6] = 0; udp[7] = 0;
     offset += UDP_HDR_LEN;
 
     // --- Payload ---
     uint8_t *payload = pkt + offset;
     memcpy(payload, &seq, sizeof(seq));
 
     prbs31_init(PRBS31_SEED + (uint32_t)(seq & 0xFFFFFFFF));
     uint16_t prbs_len = PAYLOAD_SIZE - SEQ_BYTES - 1;
     for (uint16_t i = 0; i < prbs_len; i++)
         payload[SEQ_BYTES + i] = prbs31_next_byte();
 
     payload[PAYLOAD_SIZE - 1] = calc_dtn_seq(seq);
 }
 
 // ==========================================
 // PAKET TRACE
 // ==========================================
 static void trace_packet(const uint8_t *pkt, uint16_t len, uint64_t seq, const char* label)
 {
     uint16_t tci = ((uint16_t)pkt[14] << 8) | pkt[15];
     uint16_t vlan_id = tci & 0x0FFF;
 
     printf("╔══════════════════════════════════════════════════╗\n");
     printf("║ [%s] SEQ=%-6lu  PktLen=%u  VLAN=%u  VL-ID=%u\n", label, seq, len, vlan_id, VL_ID);
     printf("╠══════════════════════════════════════════════════╣\n");
     printf("  SRC MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            pkt[6], pkt[7], pkt[8], pkt[9], pkt[10], pkt[11]);
     printf("╚══════════════════════════════════════════════════╝\n\n");
 }
 
 // ==========================================
 // MAIN
 // ==========================================
 int main(void)
 {
     printf("=== Dual Packet TX (Batch Mode) ===\n");
     printf("Interface: %s\n", INTERFACE);
     printf("Senaryo: Tek islemci cagirisiyla cift paket basimi\n");
     printf("===================================\n\n");
 
     int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
     if (sock < 0) {
         perror("socket hatasi");
         return 1;
     }
 
     struct ifreq ifr;
     memset(&ifr, 0, sizeof(ifr));
     strncpy(ifr.ifr_name, INTERFACE, IFNAMSIZ - 1);
     if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
         perror("ioctl hatasi (Interface bulunamadi)");
         close(sock);
         return 1;
     }
 
     struct sockaddr_ll saddr;
     memset(&saddr, 0, sizeof(saddr));
     saddr.sll_family = AF_PACKET;
     saddr.sll_protocol = htons(ETH_P_ALL);
     saddr.sll_ifindex = ifr.ifr_ifindex;
     saddr.sll_halen = 6;
 
     uint8_t dst_mac_prefix[] = DST_MAC_PREFIX;
     memcpy(saddr.sll_addr, dst_mac_prefix, 4);
     saddr.sll_addr[4] = (VL_ID >> 8) & 0xFF;
     saddr.sll_addr[5] = VL_ID & 0xFF;
 
     uint8_t pkt1[PACKET_SIZE];
     uint8_t pkt2[PACKET_SIZE];
     uint8_t src_mac_1[6] = SRC_MAC_1;
     uint8_t src_mac_2[6] = SRC_MAC_2;
     uint64_t seq = 0;
 
     // Gonderilecek mesajlar icin baslik dizisi ve I/O vektorleri (Batch gonderim bilesenleri)
     struct mmsghdr msgs[2];
     struct iovec iovecs[2];
 
     printf("Gonderim basliyor... (Ctrl+C ile durdur)\n\n");
 
     while (1) {
         // 1. Paketleri RAM'de hazirla
         build_packet(pkt1, seq, VLAN_ID_1, src_mac_1);
         build_packet(pkt2, seq, VLAN_ID_2, src_mac_2);
 
         // 2. Ekrana logla
         trace_packet(pkt1, PACKET_SIZE, seq, "TX-1");
         trace_packet(pkt2, PACKET_SIZE, seq, "TX-2");
 
         // 3. Batch gonderim struct'larini sifirla ve doldur
         memset(msgs, 0, sizeof(msgs));
 
         // Paket 1 Baglantisi
         iovecs[0].iov_base = pkt1;
         iovecs[0].iov_len = PACKET_SIZE;
         msgs[0].msg_hdr.msg_name = &saddr;
         msgs[0].msg_hdr.msg_namelen = sizeof(saddr);
         msgs[0].msg_hdr.msg_iov = &iovecs[0];
         msgs[0].msg_hdr.msg_iovlen = 1;
 
         // Paket 2 Baglantisi
         iovecs[1].iov_base = pkt2;
         iovecs[1].iov_len = PACKET_SIZE;
         msgs[1].msg_hdr.msg_name = &saddr;
         msgs[1].msg_hdr.msg_namelen = sizeof(saddr);
         msgs[1].msg_hdr.msg_iov = &iovecs[1];
         msgs[1].msg_hdr.msg_iovlen = 1;
 
         // 4. Tek System Call ile ag donanimina bas
         int retval = sendmmsg(sock, msgs, 2, 0);
 
         if (retval == -1) {
             perror("sendmmsg hatasi");
         } else {
             printf("  -> %d adet paket tek seferde donanima iletildi.\n\n", retval);
         }
 
         seq++;
         sleep(TX_INTERVAL_SEC);
     }
 
     close(sock);
     return 0;
 }
