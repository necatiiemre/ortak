#include <health_monitor.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>

// ============================================================================
// BE → host endian dönüştürücüler
// ----------------------------------------------------------------------------
// CMC firmware HM struct'larını wire'a big-endian yazıyor; x86_64 (LE) host'ta
// memcpy sonrası tüm multi-byte alanlar swap edilmeli. uint8 alanlarda swap
// gereksiz.
// ============================================================================
static inline uint64_t be64(uint64_t v) { return __builtin_bswap64(v); }
static inline uint32_t be32(uint32_t v) { return __builtin_bswap32(v); }
static inline uint16_t be16(uint16_t v) { return __builtin_bswap16(v); }

static void swap_pcs(Pcs_profile_stats *p)
{
    p->sample_count     = be64(p->sample_count);
    p->latest_read_time = be64(p->latest_read_time);
    p->total_run_time   = be64(p->total_run_time);
    // cpu_exec_time: 4 × Pcs_monitor_type (percentage uint8 swap gereksiz)
    p->cpu_exec_time.min_exec_time.usage  = be64(p->cpu_exec_time.min_exec_time.usage);
    p->cpu_exec_time.max_exec_time.usage  = be64(p->cpu_exec_time.max_exec_time.usage);
    p->cpu_exec_time.avg_exec_time.usage  = be64(p->cpu_exec_time.avg_exec_time.usage);
    p->cpu_exec_time.last_exec_time.usage = be64(p->cpu_exec_time.last_exec_time.usage);
    // heap_mem / stack_mem: 3 × size_t (x86_64'te 8 byte)
    p->heap_mem.total_size     = be64(p->heap_mem.total_size);
    p->heap_mem.used_size      = be64(p->heap_mem.used_size);
    p->heap_mem.max_used_size  = be64(p->heap_mem.max_used_size);
    p->stack_mem.total_size    = be64(p->stack_mem.total_size);
    p->stack_mem.used_size     = be64(p->stack_mem.used_size);
    p->stack_mem.max_used_size = be64(p->stack_mem.max_used_size);
}

static void swap_es(tA664ESMonitoring *e)
{
    e->A664_ES_FW_VER         = be64(e->A664_ES_FW_VER);
    e->A664_ES_DEV_ID         = be64(e->A664_ES_DEV_ID);
    e->A664_ES_MODE           = be64(e->A664_ES_MODE);
    e->A664_ES_CONFIG_ID      = be64(e->A664_ES_CONFIG_ID);
    e->A664_ES_BIT_STATUS     = be64(e->A664_ES_BIT_STATUS);
    e->A664_ES_CONFIG_STATUS  = be64(e->A664_ES_CONFIG_STATUS);
    // A664_BSP_CONFIG_STATUS uint8 — swap gereksiz
    e->A664_PTP_CONFIG_ID  = be16(e->A664_PTP_CONFIG_ID);
    // PTP_DEVICE_TYPE, PTP_RC_STATUS, PTP_PORT_A/B_SYNC uint8 — swap gereksiz
    e->A664_PTP_SYNC_VL_ID = be16(e->A664_PTP_SYNC_VL_ID);
    e->A664_PTP_REQ_VL_ID  = be16(e->A664_PTP_REQ_VL_ID);
    e->A664_PTP_RES_VL_ID  = be16(e->A664_PTP_RES_VL_ID);
    // A664_PTP_TOD_NETWORK uint8 — swap gereksiz
    e->A664_ES_HW_TEMP    = be32(e->A664_ES_HW_TEMP);
    e->A664_ES_HW_VCC_INT = be32(e->A664_ES_HW_VCC_INT);
    e->A664_ES_PORT_SPEED    = be64(e->A664_ES_PORT_SPEED);
    e->A664_ES_PORT_A_STATUS = be64(e->A664_ES_PORT_A_STATUS);
    e->A664_ES_PORT_B_STATUS = be64(e->A664_ES_PORT_B_STATUS);
    e->A664_ES_TX_INCOMING_COUNT       = be64(e->A664_ES_TX_INCOMING_COUNT);
    e->A664_ES_TX_A_OUTGOING_COUNT     = be64(e->A664_ES_TX_A_OUTGOING_COUNT);
    e->A664_ES_TX_B_OUTGOING_COUNT     = be64(e->A664_ES_TX_B_OUTGOING_COUNT);
    e->A664_ES_TX_VLID_DROP_COUNT      = be64(e->A664_ES_TX_VLID_DROP_COUNT);
    e->A664_ES_TX_LMIN_LMAX_DROP_COUNT = be64(e->A664_ES_TX_LMIN_LMAX_DROP_COUNT);
    e->A664_ES_TX_MAX_JITTER_DROP_COUNT = be64(e->A664_ES_TX_MAX_JITTER_DROP_COUNT);
    e->A664_ES_RX_A_INCOMING_COUNT = be64(e->A664_ES_RX_A_INCOMING_COUNT);
    e->A664_ES_RX_B_INCOMING_COUNT = be64(e->A664_ES_RX_B_INCOMING_COUNT);
    e->A664_ES_RX_OUTGOING_COUNT   = be64(e->A664_ES_RX_OUTGOING_COUNT);
    e->A664_ES_RX_A_VLID_DROP_COUNT         = be64(e->A664_ES_RX_A_VLID_DROP_COUNT);
    e->A664_ES_RX_A_LMIN_LMAX_DROP_COUNT    = be64(e->A664_ES_RX_A_LMIN_LMAX_DROP_COUNT);
    e->A664_ES_RX_A_NET_ERR_COUNT           = be64(e->A664_ES_RX_A_NET_ERR_COUNT);
    e->A664_ES_RX_A_SEQ_ERR_COUNT           = be64(e->A664_ES_RX_A_SEQ_ERR_COUNT);
    e->A664_ES_RX_A_CRC_ERROR_COUNT         = be64(e->A664_ES_RX_A_CRC_ERROR_COUNT);
    e->A664_ES_RX_A_IP_CHECKSUM_ERROR_COUNT = be64(e->A664_ES_RX_A_IP_CHECKSUM_ERROR_COUNT);
    e->A664_ES_RX_B_VLID_DROP_COUNT         = be64(e->A664_ES_RX_B_VLID_DROP_COUNT);
    e->A664_ES_RX_B_LMIN_LMAX_DROP_COUNT    = be64(e->A664_ES_RX_B_LMIN_LMAX_DROP_COUNT);
    e->A664_ES_RX_B_SEQ_ERR_COUNT           = be64(e->A664_ES_RX_B_SEQ_ERR_COUNT);
    e->A664_ES_RX_B_NET_ERR_COUNT           = be64(e->A664_ES_RX_B_NET_ERR_COUNT);
    e->A664_ES_RX_B_CRC_ERROR_COUNT         = be64(e->A664_ES_RX_B_CRC_ERROR_COUNT);
    e->A664_ES_RX_B_IP_CHECKSUM_ERROR_COUNT = be64(e->A664_ES_RX_B_IP_CHECKSUM_ERROR_COUNT);
    e->A664_BSP_TX_PACKET_COUNT       = be64(e->A664_BSP_TX_PACKET_COUNT);
    e->A664_BSP_TX_BYTE_COUNT         = be64(e->A664_BSP_TX_BYTE_COUNT);
    e->A664_BSP_TX_ERROR_COUNT        = be64(e->A664_BSP_TX_ERROR_COUNT);
    e->A664_BSP_RX_PACKET_COUNT       = be64(e->A664_BSP_RX_PACKET_COUNT);
    e->A664_BSP_RX_BYTE_COUNT         = be64(e->A664_BSP_RX_BYTE_COUNT);
    e->A664_BSP_RX_ERROR_COUNT        = be64(e->A664_BSP_RX_ERROR_COUNT);
    e->A664_BSP_RX_MISSED_FRAME_COUNT = be64(e->A664_BSP_RX_MISSED_FRAME_COUNT);
    e->A664_BSP_VER                   = be64(e->A664_BSP_VER);
    e->A664_ES_VENDOR_TYPE            = be64(e->A664_ES_VENDOR_TYPE);
    e->A664_ES_BSP_QUEUING_RX_VL_PORT_DROP_COUNT =
        be64(e->A664_ES_BSP_QUEUING_RX_VL_PORT_DROP_COUNT);
    // A664_SW_ES_ENABLE bool_gt — int boyutunda enum (4 byte): yine swap edelim.
    {
        uint32_t *raw = (uint32_t *)(void *)&e->A664_SW_ES_ENABLE;
        *raw = be32(*raw);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
static void swap_counters_dpm(COUNTERS_DPM *c)
{
    for (int i = 0; i < SRC_MAX_CONN; i++) {
        c->send_count[i]      = be64(c->send_count[i]);
        c->send_fail_count[i] = be64(c->send_fail_count[i]);
        c->receive_count[i]   = be64(c->receive_count[i]);
        c->crc_pass_count[i]  = be64(c->crc_pass_count[i]);
        c->crc_fail_count[i]  = be64(c->crc_fail_count[i]);
        c->pkg_drop_count[i]  = be64(c->pkg_drop_count[i]);
    }
}

static void swap_counters_dsm(COUNTERS_DSM *c)
{
    for (int i = 0; i < DST_MAX_CONN; i++) {
        c->send_count[i]      = be64(c->send_count[i]);
        c->send_fail_count[i] = be64(c->send_fail_count[i]);
        c->receive_count[i]   = be64(c->receive_count[i]);
        c->crc_pass_count[i]  = be64(c->crc_pass_count[i]);
        c->crc_fail_count[i]  = be64(c->crc_fail_count[i]);
        c->pkg_drop_count[i]  = be64(c->pkg_drop_count[i]);
    }
}
#pragma GCC diagnostic pop

// ============================================================================
// "unknown_len" teşhis tablosu
// ----------------------------------------------------------------------------
// Beklenmeyen toplam uzunluğa sahip paketler için (vl_id, len) çiftini
// histograma yazar; bucket'a ilk düşen paketin ilk 64 byte'ını hex dump için
// saklar. Dashboard'da bir tablo halinde basılır. Aynı çift tekrarlanırsa
// yalnızca sayaç artar (hex dump'ın üzerine yazılmaz).
// ============================================================================
#define HM_UNKNOWN_BUCKETS    32
#define HM_UNKNOWN_DUMP_LEN   64

typedef struct {
    uint16_t vl_id;
    uint16_t len;
    uint64_t count;
    uint16_t dump_len;
    uint8_t  dump[HM_UNKNOWN_DUMP_LEN];
} hm_unknown_bucket_t;

static hm_unknown_bucket_t g_unknown[HM_UNKNOWN_BUCKETS];
static int                 g_unknown_count    = 0;
static volatile uint64_t   g_unknown_overflow = 0;
static pthread_mutex_t     g_unknown_lock     = PTHREAD_MUTEX_INITIALIZER;

static void unknown_record(uint16_t vl_id, uint16_t len, const uint8_t *payload)
{
    pthread_mutex_lock(&g_unknown_lock);
    for (int i = 0; i < g_unknown_count; i++) {
        if (g_unknown[i].vl_id == vl_id && g_unknown[i].len == len) {
            g_unknown[i].count++;
            pthread_mutex_unlock(&g_unknown_lock);
            return;
        }
    }
    if (g_unknown_count < HM_UNKNOWN_BUCKETS) {
        hm_unknown_bucket_t *b = &g_unknown[g_unknown_count++];
        b->vl_id = vl_id;
        b->len   = len;
        b->count = 1;
        uint16_t n = (len < HM_UNKNOWN_DUMP_LEN) ? len : HM_UNKNOWN_DUMP_LEN;
        if (payload != NULL && n > 0) {
            memcpy(b->dump, payload, n);
        }
        b->dump_len = n;
    } else {
        __atomic_add_fetch(&g_unknown_overflow, 1, __ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&g_unknown_lock);
}

static void unknown_dump_dashboard(void)
{
    pthread_mutex_lock(&g_unknown_lock);
    if (g_unknown_count == 0) {
        pthread_mutex_unlock(&g_unknown_lock);
        return;
    }
    printf("\n[HM] UNKNOWN-LEN observed (vl_id, total_len) histogram:\n");
    printf("     expected sizes:  PCS=183  ES_MON=399  DPM=1007  DSM=431\n");
    for (int i = 0; i < g_unknown_count; i++) {
        hm_unknown_bucket_t *b = &g_unknown[i];
        printf("  VL %5u  len=%-5u  count=%-8" PRIu64 "  first %u bytes:\n",
               (unsigned)b->vl_id, (unsigned)b->len,
               b->count, (unsigned)b->dump_len);
        for (unsigned j = 0; j < b->dump_len; j++) {
            if (j % 16 == 0) printf("    %04x: ", j);
            printf("%02x ", b->dump[j]);
            if (j % 16 == 15 || j + 1 == b->dump_len) printf("\n");
        }
    }
    uint64_t ov = __atomic_load_n(&g_unknown_overflow, __ATOMIC_RELAXED);
    if (ov > 0) {
        printf("  (bucket overflow — %" PRIu64 " distinct (vl_id, len) pairs lost; "
               "increase HM_UNKNOWN_BUCKETS)\n", ov);
    }
    pthread_mutex_unlock(&g_unknown_lock);
}

// ============================================================================
// Mutex-korumalı ring buffer (multi-producer, single-consumer)
// ----------------------------------------------------------------------------
// RX worker'lar (her port için ayrı lcore) hm_handle_packet() içinden push;
// main thread dashboard'da drain. 200 pps toplam yükte mutex contention ihmal
// edilebilir; lock-free için DPDK rte_ring kullanılabilir ama codebase'in geri
// kalanı (PsuTelemetryReceiver pthread_spinlock + eski HM pthread_mutex)
// pthread primitif'leri kullandığı için tutarlılık adına bu da pthread_mutex.
// ============================================================================

static hm_queue_item_t g_ring[HM_RING_CAPACITY];
static size_t          g_ring_head = 0;   // next slot to write
static size_t          g_ring_tail = 0;   // next slot to read
static pthread_mutex_t g_ring_lock = PTHREAD_MUTEX_INITIALIZER;

// Diagnostic sayaçlar (RELAXED atomic, lock dışında)
static volatile uint64_t g_hm_rx_total          = 0;
static volatile uint64_t g_hm_rx_pcs            = 0;
static volatile uint64_t g_hm_rx_counters_dpm   = 0;
static volatile uint64_t g_hm_rx_counters_dsm   = 0;
static volatile uint64_t g_hm_rx_es_mon         = 0;
static volatile uint64_t g_hm_rx_sw_mon         = 0;
static volatile uint64_t g_hm_rx_unknown_len    = 0;
static volatile uint64_t g_hm_rx_sw_on_wrong_vl = 0;
static volatile uint64_t g_hm_rx_drop           = 0;

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Push: ring dolu ise item düşer, g_hm_rx_drop artar.
static bool hm_ring_push(const hm_queue_item_t *item)
{
    bool ok = false;
    pthread_mutex_lock(&g_ring_lock);
    size_t next = (g_ring_head + 1) % HM_RING_CAPACITY;
    if (next != g_ring_tail) {
        g_ring[g_ring_head] = *item;
        g_ring_head = next;
        ok = true;
    }
    pthread_mutex_unlock(&g_ring_lock);
    if (!ok) {
        __atomic_add_fetch(&g_hm_rx_drop, 1, __ATOMIC_RELAXED);
    }
    return ok;
}

// Drain: en fazla max item'ı out'a kopyalar, ring'i boşaltır.
static size_t hm_ring_drain(hm_queue_item_t *out, size_t max)
{
    size_t n = 0;
    pthread_mutex_lock(&g_ring_lock);
    while (n < max && g_ring_tail != g_ring_head) {
        out[n++] = g_ring[g_ring_tail];
        g_ring_tail = (g_ring_tail + 1) % HM_RING_CAPACITY;
    }
    pthread_mutex_unlock(&g_ring_lock);
    return n;
}

// ============================================================================
// RX fast-path
// ----------------------------------------------------------------------------
// rx_worker hm_handle_packet'a UDP payload başlangıcını geçer (DPDK fast-path
// L2+VLAN+IP+UDP'yi zaten skip etmiştir — payload_off=46). UDP payload'unun
// içinde ek bir application header YOK; struct doğrudan başlar, en sonda
// 1 byte sequence trailer vardır. Tür ayrımı toplam payload uzunluğuna göre:
//
//   137 B = 136  (Pcs_profile_stats, natural-aligned) + 1 (seq)
//   353 B = 352  (tA664ESMonitoring, natural-aligned) + 1 (seq)
//   961 B = 960  (COUNTERS_DPM,      packed 6×20×8)   + 1 (seq)
//   385 B = 384  (COUNTERS_DSM,      packed 6× 8×8)   + 1 (seq)   [formatı revize edilecek]
//
// SW_MON (tA664SWMonitoring, 2344 B + 1 = 2345) firmware tarafından henüz
// gönderilmediği için dispatch'i yorum satırında.
// ============================================================================
#define HM_DTN_HEADER_LEN     0     // UDP payload zaten DTN header'sız geliyor
#define HM_DTN_TRAILER_LEN    1     // pakette son 1 byte: sequence number
#define HM_FRAME_OVERHEAD     (HM_DTN_HEADER_LEN + HM_DTN_TRAILER_LEN)

#define HM_PCS_TOTAL_LEN          (HM_FRAME_OVERHEAD + (uint16_t)sizeof(Pcs_profile_stats))   // 183
#define HM_ES_MON_TOTAL_LEN       (HM_FRAME_OVERHEAD + (uint16_t)sizeof(tA664ESMonitoring))   // 399
#define HM_COUNTERS_DPM_TOTAL_LEN (HM_FRAME_OVERHEAD + (uint16_t)sizeof(COUNTERS_DPM))        // 1007
#define HM_COUNTERS_DSM_TOTAL_LEN (HM_FRAME_OVERHEAD + (uint16_t)sizeof(COUNTERS_DSM))        // 1007

void hm_handle_packet(uint16_t vl_id, const uint8_t *payload, uint16_t len)
{
    __atomic_add_fetch(&g_hm_rx_total, 1, __ATOMIC_RELAXED);

    if (payload == NULL || len < HM_FRAME_OVERHEAD) {
        __atomic_add_fetch(&g_hm_rx_unknown_len, 1, __ATOMIC_RELAXED);
        unknown_record(vl_id, len, payload);
        return;
    }

    const uint8_t *body = payload + HM_DTN_HEADER_LEN;

    hm_queue_item_t item;
    memset(&item, 0, sizeof(item));
    item.vl_id           = vl_id;
    item.rx_timestamp_ns = now_ns();

    if (len == HM_PCS_TOTAL_LEN) {
        item.kind = HM_ITEM_PCS_PROFILE;
        memcpy(&item.payload.pcs, body, sizeof(Pcs_profile_stats));
        swap_pcs(&item.payload.pcs);
        __atomic_add_fetch(&g_hm_rx_pcs, 1, __ATOMIC_RELAXED);
    }
    else if (len == HM_ES_MON_TOTAL_LEN) {
        item.kind = HM_ITEM_DTN_ES_MONITORING;
        memcpy(&item.payload.es_mon, body, sizeof(tA664ESMonitoring));
        swap_es(&item.payload.es_mon);
        __atomic_add_fetch(&g_hm_rx_es_mon, 1, __ATOMIC_RELAXED);
    }
    else if (len == HM_COUNTERS_DPM_TOTAL_LEN) {
        item.kind = HM_ITEM_COUNTERS_DPM;
        memcpy(&item.payload.counters_dpm, body, sizeof(COUNTERS_DPM));
        swap_counters_dpm(&item.payload.counters_dpm);
        __atomic_add_fetch(&g_hm_rx_counters_dpm, 1, __ATOMIC_RELAXED);
    }
    else if (len == HM_COUNTERS_DSM_TOTAL_LEN) {
        item.kind = HM_ITEM_COUNTERS_DSM;
        memcpy(&item.payload.counters_dsm, body, sizeof(COUNTERS_DSM));
        swap_counters_dsm(&item.payload.counters_dsm);
        __atomic_add_fetch(&g_hm_rx_counters_dsm, 1, __ATOMIC_RELAXED);
    }
    // SW_MON şimdilik firmware'dan gelmiyor — açıldığında:
    // else if (len == HM_FRAME_OVERHEAD + (uint16_t)sizeof(tA664SWMonitoring)) {
    //     if (!hm_vl_id_accepts_sw_mon(vl_id)) {
    //         __atomic_add_fetch(&g_hm_rx_sw_on_wrong_vl, 1, __ATOMIC_RELAXED);
    //         return;
    //     }
    //     item.kind = HM_ITEM_DTN_SW_MONITORING;
    //     memcpy(&item.payload.sw_mon, body, sizeof(tA664SWMonitoring));
    //     __atomic_add_fetch(&g_hm_rx_sw_mon, 1, __ATOMIC_RELAXED);
    // }
    else {
        __atomic_add_fetch(&g_hm_rx_unknown_len, 1, __ATOMIC_RELAXED);
        unknown_record(vl_id, len, payload);
        return;
    }

    hm_ring_push(&item);
}

// ============================================================================
// Dashboard — main thread'den 1 Hz çağrılır.
// ----------------------------------------------------------------------------
// Ring'i tek seferde drain eder, item'ları sırasıyla yazdırır, sonunda diag
// satırı basar. Stack'te 256 item × ~3.5 KB ≈ 900 KB; daha güvenli olsun diye
// static.
// ============================================================================
void hm_print_dashboard(void)
{
    static uint64_t        tick = 0;
    static hm_queue_item_t drain_buf[HM_RING_CAPACITY];

    size_t n = hm_ring_drain(drain_buf, HM_RING_CAPACITY);

    // Aynı (vl_id, kind) için bir tick içinde gelen birden çok paketten
    // SADECE en sonuncusunu yazdır; başlıkta "×N" notu olarak toplam sayı.
    // En kötü durum 256 item × 256 item lineer arama ~64K karşılaştırma — bu
    // ölçekte ihmal edilebilir.
    struct dedup_entry {
        uint16_t       vl_id;
        hm_item_kind_t kind;
        size_t         last_idx;
        unsigned       count;
    };
    static struct dedup_entry dedup[HM_RING_CAPACITY];
    size_t dedup_n = 0;

    for (size_t i = 0; i < n; i++) {
        const hm_queue_item_t *it = &drain_buf[i];
        if (it->kind == HM_ITEM_NONE) continue;
        size_t found = (size_t)-1;
        for (size_t j = 0; j < dedup_n; j++) {
            if (dedup[j].vl_id == it->vl_id && dedup[j].kind == it->kind) {
                found = j;
                break;
            }
        }
        if (found != (size_t)-1) {
            dedup[found].last_idx = i;
            dedup[found].count++;
        } else {
            dedup[dedup_n].vl_id    = it->vl_id;
            dedup[dedup_n].kind     = it->kind;
            dedup[dedup_n].last_idx = i;
            dedup[dedup_n].count    = 1;
            dedup_n++;
        }
    }

    printf("\n\n");
    printf("################################################################################\n");
    printf("###  CMC HEALTH MONITOR DASHBOARD — tick %-6lu  drained=%-4zu  unique=%-3zu     ###\n",
           (unsigned long)tick, n, dedup_n);
    printf("################################################################################\n");

    for (size_t j = 0; j < dedup_n; j++) {
        const hm_queue_item_t *it = &drain_buf[dedup[j].last_idx];
        unsigned cnt = dedup[j].count;
        switch (it->kind) {
            case HM_ITEM_PCS_PROFILE:
                print_pcs_profile_stats(&it->payload.pcs, it->vl_id, cnt);
                break;
            case HM_ITEM_COUNTERS_DPM:
                print_counters_dpm(&it->payload.counters_dpm, it->vl_id, cnt);
                break;
            case HM_ITEM_COUNTERS_DSM:
                print_counters_dsm(&it->payload.counters_dsm, it->vl_id, cnt);
                break;
            case HM_ITEM_DTN_ES_MONITORING:
                print_dtn_es_monitoring(&it->payload.es_mon, it->vl_id, cnt);
                break;
            case HM_ITEM_DTN_SW_MONITORING:
                print_dtn_sw_monitoring(&it->payload.sw_mon, it->vl_id, cnt);
                break;
            case HM_ITEM_NONE:
            default:
                break;
        }
    }

    uint64_t total       = __atomic_load_n(&g_hm_rx_total,          __ATOMIC_RELAXED);
    uint64_t pcs_cnt     = __atomic_load_n(&g_hm_rx_pcs,             __ATOMIC_RELAXED);
    uint64_t dpm_cnt     = __atomic_load_n(&g_hm_rx_counters_dpm,    __ATOMIC_RELAXED);
    uint64_t dsm_cnt     = __atomic_load_n(&g_hm_rx_counters_dsm,    __ATOMIC_RELAXED);
    uint64_t es_cnt      = __atomic_load_n(&g_hm_rx_es_mon,          __ATOMIC_RELAXED);
    uint64_t sw_cnt      = __atomic_load_n(&g_hm_rx_sw_mon,          __ATOMIC_RELAXED);
    uint64_t unknown_len = __atomic_load_n(&g_hm_rx_unknown_len,     __ATOMIC_RELAXED);
    uint64_t sw_wrong    = __atomic_load_n(&g_hm_rx_sw_on_wrong_vl,  __ATOMIC_RELAXED);
    uint64_t drops       = __atomic_load_n(&g_hm_rx_drop,            __ATOMIC_RELAXED);

    printf("[HM] tick=%lu total=%lu pcs=%lu dpm=%lu dsm=%lu es_mon=%lu sw_mon=%lu "
           "unknown_len=%lu sw_on_wrong_vl=%lu drops=%lu drained=%zu\n",
           (unsigned long)tick,
           (unsigned long)total,
           (unsigned long)pcs_cnt,
           (unsigned long)dpm_cnt,
           (unsigned long)dsm_cnt,
           (unsigned long)es_cnt,
           (unsigned long)sw_cnt,
           (unsigned long)unknown_len,
           (unsigned long)sw_wrong,
           (unsigned long)drops,
           n);

    unknown_dump_dashboard();

    fflush(stdout);
    tick++;
}
