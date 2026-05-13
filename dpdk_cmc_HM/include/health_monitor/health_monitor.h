#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <cmc_es_monitoring.h>   // tA664ESMonitoring
#include <cmc_sw_monitoring.h>   // tA664SWMonitoring (status + port[12])
#include <cmc_other_types.h>     // Pcs_profile_stats, COUNTERS

// ============================================================================
// CMC Health Monitor
// ----------------------------------------------------------------------------
// HM trafiği ayrı bir interface değil; normal DPDK data-plane portları üzerinde
// taşınır. rx_worker fast-path'inde VL-ID set kontrolü ile yakalanıp
// hm_handle_packet()'e düşer. PRBS doğrulama yolundan çıkar.
//
// VL-ID'ler iki bloğa ayrılır:
//   * 2021, 2042, 2063, 2084, 2105 → ES_MON / PCS / COUNTERS paketleri
//   * 8009, 8109                   → ES_MON / PCS / COUNTERS + ek olarak SW_MON
//
// Aynı VL-ID üzerinden farklı struct'lar farklı paketlerde gelir; tür ayrımı
// payload uzunluğuna göre yapılır.
// ============================================================================

// HM VL-ID set (rx_worker erken-dallanması için)
#define HM_VLID_2021   2021
#define HM_VLID_2042   2042
#define HM_VLID_2063   2063
#define HM_VLID_2084   2084
#define HM_VLID_2105   2105
#define HM_VLID_8009   8009
#define HM_VLID_8109   8109

// True ⇒ paket HM dispatch'ine girer (PRBS path'inden çıkar).
static inline bool hm_is_health_monitor_vl_id(uint16_t vl_id)
{
    switch (vl_id) {
        case HM_VLID_2021:
        case HM_VLID_2042:
        case HM_VLID_2063:
        case HM_VLID_2084:
        case HM_VLID_2105:
        case HM_VLID_8009:
        case HM_VLID_8109:
            return true;
        default:
            return false;
    }
}

// SW_MON sadece 8009 ve 8109'dan beklenir.
static inline bool hm_vl_id_accepts_sw_mon(uint16_t vl_id)
{
    return (vl_id == HM_VLID_8009 || vl_id == HM_VLID_8109);
}

// Dashboard basım aralığı (ms). Main loop sleep(1) ile uyumlu.
#define HEALTH_MONITOR_DASHBOARD_INTERVAL_MS 1000

// ============================================================================
// Queue item — tagged union. RX worker doldurur, dashboard tüketir.
// ============================================================================
typedef enum {
    HM_ITEM_NONE = 0,
    HM_ITEM_PCS_PROFILE,        // Pcs_profile_stats
    HM_ITEM_COUNTERS_DPM,       // COUNTERS_DPM
    HM_ITEM_COUNTERS_DSM,       // COUNTERS_DSM
    HM_ITEM_DTN_ES_MONITORING,  // tA664ESMonitoring
    HM_ITEM_DTN_SW_MONITORING,  // tA664SWMonitoring
} hm_item_kind_t;

typedef struct {
    hm_item_kind_t kind;
    uint16_t       vl_id;
    uint64_t       rx_timestamp_ns;
    union {
        Pcs_profile_stats   pcs;
        COUNTERS_DPM        counters_dpm;
        COUNTERS_DSM        counters_dsm;
        tA664ESMonitoring   es_mon;
        tA664SWMonitoring   sw_mon;   // status + port[12]
    } payload;
} hm_queue_item_t;

// Ring kapasitesi. ~200 pps'lik HM yükü için fazlasıyla yeterli.
#define HM_RING_CAPACITY 256

// ============================================================================
// RX fast-path entry — rx_worker'dan çağrılır.
//   vl_id    : pakette extract edilmiş VL-ID
//   payload  : UDP payload başı
//   len      : UDP payload uzunluğu (bayt)
// Tür payload uzunluğuna göre belirlenir; bilinmeyen uzunluk → drop + sayaç.
// ============================================================================
void hm_handle_packet(uint16_t vl_id, const uint8_t *payload, uint16_t len);

// ============================================================================
// Dashboard — main thread'den 1 Hz çağrılır. Ring'i drain eder, her item için
// uygun print_* fonksiyonunu çağırır, sonunda tick + diag sayaç satırı basar.
// ============================================================================
void hm_print_dashboard(void);

// ============================================================================
// Print fonksiyonları — health_monitor_cmc.c içinde.
// vl_id: paketin geldiği VL-ID, başlıkta gösterilir.
// packets: bu tick'te aynı (vl_id, kind) çiftinden kaç paket dedup edildi
//          (başlıkta "×N" notu olarak görünür). 1 ise not yazılmaz.
// ============================================================================
void print_pcs_profile_stats   (const Pcs_profile_stats   *data, uint16_t vl_id, unsigned packets);
void print_counters_dpm        (const COUNTERS_DPM        *data, uint16_t vl_id, unsigned packets);
void print_counters_dsm        (const COUNTERS_DSM        *data, uint16_t vl_id, unsigned packets);
void print_dtn_es_monitoring   (const tA664ESMonitoring   *data, uint16_t vl_id, unsigned packets);
void print_dtn_sw_monitoring   (const tA664SWMonitoring   *data, uint16_t vl_id, unsigned packets);

#endif /* HEALTH_MONITOR_H */