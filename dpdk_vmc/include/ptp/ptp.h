#ifndef PTP_H
#define PTP_H

#include <stdint.h>
#include <stdbool.h>
#include <rte_mempool.h>

/* Two roles, per flow. */
enum ptp_role {
    PTP_ROLE_M1_MASTER = 0,   /* RX Sync (passive), RX Delay_Req, TX Delay_Resp */
    PTP_ROLE_M2_SLAVE  = 1,   /* RX Sync, TX Delay_Req, RX Delay_Resp */
};

/* Network element identifiers (encoded in source MAC byte 5). */
#define PTP_NE_A 0x20
#define PTP_NE_B 0x40

/* Dedicated NIC queue indices used by the PTP fast path.
 * Selected to fit beyond the PRBS/data queues (which use 0 .. NUM_*_CORES-1). */
#define PTP_RX_QUEUE_ID   NUM_RX_CORES
#define PTP_TX_QUEUE_ID   NUM_TX_CORES

/* Number of configured flows (see ptp_flows[] in Config.h). */
#define PTP_NUM_FLOWS 4

struct ptp_flow {
    enum ptp_role role;
    uint16_t      dpdk_port;
    uint8_t       ne;          /* PTP_NE_A or PTP_NE_B */
    /* Sync RX */
    uint16_t      sync_vlan;
    uint16_t      sync_vl_id;
    /* Delay_Req (M1=RX from Unit, M2=TX from us) */
    uint16_t      req_vlan;
    uint16_t      req_vl_id;
    /* Delay_Resp (M1=TX from us, M2=RX from VMC) */
    uint16_t      resp_vlan;
    uint16_t      resp_vl_id;
    const char   *j_label;
};

/* Defined in Config.h. */
extern const struct ptp_flow ptp_flows[PTP_NUM_FLOWS];

/* Returns true if the given DPDK port carries any PTP flow.
 * Used by main.c to bump nb_rx_queues/nb_tx_queues by 1. */
bool ptp_port_has_flow(uint16_t port_id);

/* Install rte_flow rules that steer EtherType=0x88F7 frames to
 * PTP_RX_QUEUE_ID on every port that carries a PTP flow.
 * Must be called after rte_eth_dev_start(). */
int ptp_flow_rules_install(uint16_t port_id);

/* One-time PTP subsystem init. Creates mempool, snapshots port MAC
 * addresses, prepares state, and spawns the polling thread.
 * Returns 0 on success. */
int ptp_init(void);

/* Stop polling thread (idempotent). */
void ptp_stop(void);

/* Print the PTP dashboard to stdout. Safe to call from the main loop. */
void ptp_print_dashboard(void);

#endif /* PTP_H */
