#include <health_monitor.h>

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

// ============================================================================
// VL-ID → kullanıcı dostu etiket
// ============================================================================
static const char *hm_vl_label(uint16_t vl_id)
{
    switch (vl_id) {
        case 2021: return "DPM-1";
        case 2042: return "DPM-2";
        case 2063: return "DPM-3";
        case 2084: return "DPM-4";
        case 2105: return "DPM-5";
        case 8009: return "DSM-A";
        case 8109: return "DSM-B";
        default:   return "VL?";
    }
}

// ============================================================================
// Decode yardımcıları (her biri kendi static buffer'ında string döner;
// aynı printf'te 2 kez kullanılabilir diye 2-slot ring)
// ============================================================================
static const char *fmt_ver(uint64_t v)
{
    static char rings[2][16];
    static int idx = 0;
    char *buf = rings[idx];
    idx = (idx + 1) & 1;
    unsigned a = (unsigned)((v >> 16) & 0xFF);
    unsigned b = (unsigned)((v >>  8) & 0xFF);
    unsigned c = (unsigned)( v        & 0xFF);
    snprintf(buf, 16, "%u.%u.%u", a, b, c);
    return buf;
}

static const char *fmt_temp(uint32_t v)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%u.%02u C", v / 100u, v % 100u);
    return buf;
}

static const char *fmt_vcc(uint32_t v)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%u.%04u V", v / 10000u, v % 10000u);
    return buf;
}

static const char *port_speed_str(uint64_t v)
{
    switch (v) {
        case 0: return "10M";
        case 1: return "100M";
        case 2: return "1G";
        default: return "?";
    }
}

static const char *ptp_dev_str(uint8_t v)
{
    switch (v) {
        case 0: return "slave";
        case 1: return "master";
        case 3: return "GM";
        default: return "?";
    }
}

static void banner(uint16_t vl_id, const char *title, unsigned packets)
{
    printf("\n========================================================================================\n");
    if (packets > 1) {
        printf("  [%s] %s  (×%u packets)\n", hm_vl_label(vl_id), title, packets);
    } else {
        printf("  [%s] %s\n", hm_vl_label(vl_id), title);
    }
    printf("========================================================================================\n");
}

static void hr(void)
{
    printf("----------------------------------------------------------------------------------------\n");
}

// ============================================================================
// Pcs_profile_stats
// ============================================================================
void print_pcs_profile_stats(const Pcs_profile_stats *d, uint16_t vl_id, unsigned packets)
{
    if (d == NULL) return;
    banner(vl_id, "PCS PROFILE STATS", packets);

    printf("  sample_count        : %" PRIu64 "\n", d->sample_count);
    printf("  latest_read_time    : %" PRIu64 " ns\n", d->latest_read_time);
    printf("  total_run_time      : %" PRIu64 " ns\n", d->total_run_time);

    hr();
    printf("  CPU EXEC TIME         %-12s %-20s\n", "percentage", "usage (ns)");
    printf("    min               : %-12u %" PRIu64 "\n",
           d->cpu_exec_time.min_exec_time.percentage,
           d->cpu_exec_time.min_exec_time.usage);
    printf("    max               : %-12u %" PRIu64 "\n",
           d->cpu_exec_time.max_exec_time.percentage,
           d->cpu_exec_time.max_exec_time.usage);
    printf("    avg               : %-12u %" PRIu64 "\n",
           d->cpu_exec_time.avg_exec_time.percentage,
           d->cpu_exec_time.avg_exec_time.usage);
    printf("    last              : %-12u %" PRIu64 "\n",
           d->cpu_exec_time.last_exec_time.percentage,
           d->cpu_exec_time.last_exec_time.usage);

    hr();
    printf("  HEAP MEM            : total=%zu  used=%zu  max_used=%zu\n",
           d->heap_mem.total_size, d->heap_mem.used_size, d->heap_mem.max_used_size);
    printf("  STACK MEM           : total=%zu  used=%zu  max_used=%zu\n",
           d->stack_mem.total_size, d->stack_mem.used_size, d->stack_mem.max_used_size);
    printf("========================================================================================\n");
}

// ============================================================================
// COUNTERS_DPM / COUNTERS_DSM
// ============================================================================
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
static void print_counters_body(const uint64_t *send, const uint64_t *send_fail,
                                const uint64_t *receive, const uint64_t *crc_pass,
                                const uint64_t *crc_fail, const uint64_t *drop,
                                unsigned rows, const char *row_label)
{
    printf("  %-4s | %-12s | %-12s | %-12s | %-12s | %-12s | %-12s\n",
           row_label, "send", "send_fail", "receive", "crc_pass", "crc_fail", "pkg_drop");
    hr();

    unsigned shown = 0;
    for (unsigned i = 0; i < rows; i++) {
        uint64_t s = send[i], sf = send_fail[i], r = receive[i];
        uint64_t cp = crc_pass[i], cf = crc_fail[i], pd = drop[i];
        if ((s | sf | r | cp | cf | pd) == 0) continue;
        printf("  %-4u | %-12" PRIu64 " | %-12" PRIu64 " | %-12" PRIu64
               " | %-12" PRIu64 " | %-12" PRIu64 " | %-12" PRIu64 "\n",
               i, s, sf, r, cp, cf, pd);
        shown++;
    }
    if (shown == 0) {
        printf("  (all %u rows are zero)\n", rows);
    }
    printf("========================================================================================\n");
}

void print_counters_dpm(const COUNTERS_DPM *d, uint16_t vl_id, unsigned packets)
{
    if (d == NULL) return;
    banner(vl_id, "INTER-LRM COUNTERS DPM", packets);
    print_counters_body(d->send_count, d->send_fail_count, d->receive_count,
                        d->crc_pass_count, d->crc_fail_count, d->pkg_drop_count,
                        SRC_MAX_CONN, "SRC");
}

void print_counters_dsm(const COUNTERS_DSM *d, uint16_t vl_id, unsigned packets)
{
    if (d == NULL) return;
    banner(vl_id, "INTER-LRM COUNTERS DSM", packets);
    print_counters_body(d->send_count, d->send_fail_count, d->receive_count,
                        d->crc_pass_count, d->crc_fail_count, d->pkg_drop_count,
                        DST_MAX_CONN, "DST");
}
#pragma GCC diagnostic pop

// ============================================================================
// tA664ESMonitoring
// ============================================================================
void print_dtn_es_monitoring(const tA664ESMonitoring *d, uint16_t vl_id, unsigned packets)
{
    if (d == NULL) return;
    banner(vl_id, "DTN ES MONITORING", packets);

    printf("[ IDENTITY ]\n");
    printf("  FW_VER              : %s   (raw=%" PRIu64 ")\n",
           fmt_ver(d->A664_ES_FW_VER), d->A664_ES_FW_VER);
    printf("  DEV_ID              : %" PRIu64 "\n", d->A664_ES_DEV_ID);
    printf("  MODE                : %" PRIu64 "\n", d->A664_ES_MODE);
    printf("  CONFIG_ID           : %" PRIu64 "\n", d->A664_ES_CONFIG_ID);
    printf("  BIT_STATUS          : %" PRIu64 "   (%s)\n",
           d->A664_ES_BIT_STATUS, d->A664_ES_BIT_STATUS == 1 ? "PASS" : "FAIL");
    printf("  CONFIG_STATUS       : %" PRIu64 "\n", d->A664_ES_CONFIG_STATUS);
    printf("  BSP_CONFIG_STATUS   : 0x%02X   (ES=%c SW=%c SW-ES=%c)\n",
           d->A664_BSP_CONFIG_STATUS,
           (d->A664_BSP_CONFIG_STATUS & 0x1) ? 'Y' : '-',
           (d->A664_BSP_CONFIG_STATUS & 0x2) ? 'Y' : '-',
           (d->A664_BSP_CONFIG_STATUS & 0x4) ? 'Y' : '-');
    printf("  VENDOR_TYPE         : %" PRIu64 "\n", d->A664_ES_VENDOR_TYPE);
    printf("  SW_ES_ENABLE        : %d\n", (int)d->A664_SW_ES_ENABLE);

    hr();
    printf("[ PTP ]\n");
    printf("  CONFIG_ID           : %u\n", d->A664_PTP_CONFIG_ID);
    printf("  DEVICE_TYPE         : %u   (%s)\n",
           d->A664_PTP_DEVICE_TYPE, ptp_dev_str(d->A664_PTP_DEVICE_TYPE));
    printf("  RC_STATUS           : %u\n", d->A664_PTP_RC_STATUS);
    printf("  PORT_A_SYNC         : %u\n", d->A664_PTP_PORT_A_SYNC);
    printf("  PORT_B_SYNC         : %u\n", d->A664_PTP_PORT_B_SYNC);
    printf("  SYNC_VL_ID          : %u\n", d->A664_PTP_SYNC_VL_ID);
    printf("  REQ_VL_ID           : %u\n", d->A664_PTP_REQ_VL_ID);
    printf("  RES_VL_ID           : %u\n", d->A664_PTP_RES_VL_ID);
    printf("  TOD_NETWORK         : %u\n", d->A664_PTP_TOD_NETWORK);

    hr();
    printf("[ HW & LINK ]\n");
    printf("  HW_TEMP             : %s   (raw=%u)\n",
           fmt_temp(d->A664_ES_HW_TEMP), d->A664_ES_HW_TEMP);
    printf("  HW_VCC_INT          : %s   (raw=%u)\n",
           fmt_vcc(d->A664_ES_HW_VCC_INT), d->A664_ES_HW_VCC_INT);
    printf("  PORT_SPEED          : %" PRIu64 "   (%s)\n",
           d->A664_ES_PORT_SPEED, port_speed_str(d->A664_ES_PORT_SPEED));
    printf("  PORT_A_STATUS       : %" PRIu64 "   (%s)\n",
           d->A664_ES_PORT_A_STATUS, d->A664_ES_PORT_A_STATUS ? "UP" : "DOWN");
    printf("  PORT_B_STATUS       : %" PRIu64 "   (%s)\n",
           d->A664_ES_PORT_B_STATUS, d->A664_ES_PORT_B_STATUS ? "UP" : "DOWN");

    hr();
    printf("[ TX COUNTERS ]\n");
    printf("  TX_INCOMING         : %" PRIu64 "\n", d->A664_ES_TX_INCOMING_COUNT);
    printf("  TX_A_OUTGOING       : %" PRIu64 "\n", d->A664_ES_TX_A_OUTGOING_COUNT);
    printf("  TX_B_OUTGOING       : %" PRIu64 "\n", d->A664_ES_TX_B_OUTGOING_COUNT);
    printf("  TX_VLID_DROP        : %" PRIu64 "\n", d->A664_ES_TX_VLID_DROP_COUNT);
    printf("  TX_LMIN_LMAX_DROP   : %" PRIu64 "\n", d->A664_ES_TX_LMIN_LMAX_DROP_COUNT);
    printf("  TX_MAX_JITTER_DROP  : %" PRIu64 "\n", d->A664_ES_TX_MAX_JITTER_DROP_COUNT);

    hr();
    printf("[ RX COUNTERS ]\n");
    printf("  RX_A_INCOMING       : %" PRIu64 "\n", d->A664_ES_RX_A_INCOMING_COUNT);
    printf("  RX_B_INCOMING       : %" PRIu64 "\n", d->A664_ES_RX_B_INCOMING_COUNT);
    printf("  RX_OUTGOING         : %" PRIu64 "\n", d->A664_ES_RX_OUTGOING_COUNT);
    printf("  Port A  drops       : vlid=%" PRIu64 " lmin/lmax=%" PRIu64 " net=%" PRIu64
           " seq=%" PRIu64 " crc=%" PRIu64 " ipchk=%" PRIu64 "\n",
           d->A664_ES_RX_A_VLID_DROP_COUNT,
           d->A664_ES_RX_A_LMIN_LMAX_DROP_COUNT,
           d->A664_ES_RX_A_NET_ERR_COUNT,
           d->A664_ES_RX_A_SEQ_ERR_COUNT,
           d->A664_ES_RX_A_CRC_ERROR_COUNT,
           d->A664_ES_RX_A_IP_CHECKSUM_ERROR_COUNT);
    printf("  Port B  drops       : vlid=%" PRIu64 " lmin/lmax=%" PRIu64 " net=%" PRIu64
           " seq=%" PRIu64 " crc=%" PRIu64 " ipchk=%" PRIu64 "\n",
           d->A664_ES_RX_B_VLID_DROP_COUNT,
           d->A664_ES_RX_B_LMIN_LMAX_DROP_COUNT,
           d->A664_ES_RX_B_NET_ERR_COUNT,
           d->A664_ES_RX_B_SEQ_ERR_COUNT,
           d->A664_ES_RX_B_CRC_ERROR_COUNT,
           d->A664_ES_RX_B_IP_CHECKSUM_ERROR_COUNT);

    hr();
    printf("[ BSP (Driver) ]\n");
    printf("  VER                 : %s   (raw=%" PRIu64 ")\n",
           fmt_ver(d->A664_BSP_VER), d->A664_BSP_VER);
    printf("  TX  pkts=%" PRIu64 "  bytes=%" PRIu64 "  errors=%" PRIu64 "\n",
           d->A664_BSP_TX_PACKET_COUNT, d->A664_BSP_TX_BYTE_COUNT,
           d->A664_BSP_TX_ERROR_COUNT);
    printf("  RX  pkts=%" PRIu64 "  bytes=%" PRIu64 "  errors=%" PRIu64 "  missed=%" PRIu64 "\n",
           d->A664_BSP_RX_PACKET_COUNT, d->A664_BSP_RX_BYTE_COUNT,
           d->A664_BSP_RX_ERROR_COUNT, d->A664_BSP_RX_MISSED_FRAME_COUNT);
    printf("  ES_BSP_QUEUING_RX_VL_PORT_DROP : %" PRIu64 "\n",
           d->A664_ES_BSP_QUEUING_RX_VL_PORT_DROP_COUNT);
    printf("========================================================================================\n");
}

// ============================================================================
// tA664SWMonitoring — firmware henüz göndermiyor; tanım korunsun.
// ============================================================================
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
void print_dtn_sw_monitoring(const tA664SWMonitoring *d, uint16_t vl_id, unsigned packets)
{
    if (d == NULL) return;
    banner(vl_id, "DTN SW MONITORING", packets);

    const a664SWMonitoringStatus *s = &d->status;

    printf("[ STATUS ]\n");
    printf("  SW_VERSION          : %s   (raw=%" PRIu64 ")\n",
           fmt_ver(s->A664_SW_VERSION), s->A664_SW_VERSION);
    printf("  SW_ES_VERSION       : %s   (raw=%" PRIu64 ")\n",
           fmt_ver(s->A664_SW_ES_VERSION), s->A664_SW_ES_VERSION);
    printf("  DEVICE_ID           : %u\n",       s->A664_SW_DEVICE_ID);
    printf("  PORT_NUM            : %u\n",       s->A664_SW_PORT_NUM);
    printf("  CONFIG_ID           : %u\n",       s->A664_SW_CONFIG_ID);
    printf("  HEARTBEAT           : %u\n",       s->A664_SW_HEARTBEAT);
    printf("  CURRENT_MODE        : %u\n",       s->A664_SW_CURRENT_MODE);
    printf("  TOKEN_BUCKET        : %u\n",       s->A664_SW_TOKEN_BUCKET_STATUS);
    printf("  AUTOMAC_UPDATE      : %u\n",       s->A664_SW_AUTOMAC_UPDATE_STATUS);
    printf("  UPSTREAM_MODE       : %u\n",       s->A664_SW_UPSTREAM_MODE_STATUS);
    printf("  VENDOR_TYPE         : %" PRIu64 "\n", (uint64_t)s->A664_SW_VENDOR_TYPE);
    printf("  TEMPERATURE         : %s   (raw=%u)\n",
           fmt_temp((uint32_t)s->A664_SW_TEMPERATURE), s->A664_SW_TEMPERATURE);
    printf("  INTERNAL_VOLTAGE    : %s   (raw=%u)\n",
           fmt_vcc((uint32_t)s->A664_SW_INTERNAL_VOLTAGE), s->A664_SW_INTERNAL_VOLTAGE);
    printf("  TRANSCEIVER_TEMP    : %" PRIu64 "\n", s->A664_SW_TRANSCEIVER_TEMP);
    printf("  SHARED_XCVR_TEMP    : %" PRIu64 "\n", s->A664_SW_SHARED_TRANSCEIVER_TEMP);
    printf("  TX_TOTAL            : %" PRIu64 "\n", s->A664_SW_TOT_TX_DATA_NUM);
    printf("  RX_TOTAL            : %" PRIu64 "\n", s->A664_SW_TOT_RX_DATA_NUM);
    printf("  TIME_OF_DAY         : %" PRIu64 " s + %" PRIu64 " ns\n",
           s->A664_SW_TIME_OF_DAY_S, s->A664_SW_TIME_OF_DAY_NS);

    hr();
    printf("[ PORTS ]  (link=0 ve tüm sayaçları sıfır olanlar gizlendi)\n");
    int any = 0;
    for (unsigned i = 0; i < (unsigned)A664_SW_MAX_PORT_COUNT; i++) {
        const a664SWMonitoringPort *p = &d->port[i];
        uint64_t sum =
            p->A664_SW_CRC_ERR_CNT | p->A664_SW_TX_FRAME_CNT |
            p->A664_SW_RX_FRAME_CNT | p->A664_SW_MAC_ERR_CNT |
            p->A664_SW_TOKEN_ERR_CNT | p->A664_SW_MAX_DELAY_ERR_CNT;
        if (p->A664_SW_PORT_LINK == 0 && sum == 0) continue;
        any = 1;
        printf("  port[%2u]            : id=%" PRIu64 "  link=%u  bit=%u  tx=%" PRIu64
               "  rx=%" PRIu64 "  crc_err=%" PRIu64 "  max_delay_err=%" PRIu64 "\n",
               i, p->A664_SW_PORT_ID, p->A664_SW_PORT_LINK, p->A664_SW_BIT_STATUS,
               p->A664_SW_TX_FRAME_CNT, p->A664_SW_RX_FRAME_CNT,
               p->A664_SW_CRC_ERR_CNT, p->A664_SW_MAX_DELAY_ERR_CNT);
    }
    if (!any) {
        printf("  (all 12 ports idle)\n");
    }
    printf("========================================================================================\n");
}
#pragma GCC diagnostic pop