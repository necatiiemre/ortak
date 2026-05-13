#ifndef CMC_ES_MONITORING_H
#define CMC_ES_MONITORING_H

#include <stdint.h>
#include <stdbool.h>

#ifndef CMC_BOOL_GT_DEFINED
#define CMC_BOOL_GT_DEFINED
typedef enum
{
#ifndef FALSE
    FALSE,
#else
    __FALSE__,
#endif
#ifndef TRUE
    TRUE
#else
    __TRUE__
#endif
} bool_gt;
#endif /* CMC_BOOL_GT_DEFINED */

// ============================================================================
// ============================================================================

// DTN ES MONITORING
// NOT: Wire formatı non-packed (natural alignment). CMC firmware'dan toplam
// 352 byte gönderiliyor (DTN header sonrası). packed bırakılırsa eksik
// kopyalama olur — bu struct'ı packed YAPMA.
typedef struct tA664ESMonitoring
{
	/**
	 * @brief Hardware firmware version.
	 */
	uint64_t A664_ES_FW_VER;

	/**
	 * @brief Device ID of DTN End System.
	 */
	uint64_t A664_ES_DEV_ID;

	/**
	 * @brief Current operation mode of DTN End System.
	 */
	uint64_t A664_ES_MODE;

	/**
	 * @brief Configuration ID of DTN End System.
	 */
	uint64_t A664_ES_CONFIG_ID;

	/**
	 * @brief Result of the built-in test of DTN Hardware.
	 */
	uint64_t A664_ES_BIT_STATUS;

	/**
 	 * @brief Status of DTN End System Configuration. If it's value is 3, that means configuration is successfully applied.
	 */
	uint64_t A664_ES_CONFIG_STATUS;

	/**
	 * @brief DTN Driver Configuration Status
	 *
	 * This field uses bit-mapped encoding to indicate configuration completion status:
	 * - Bit 0 (LSB): End System (ES) configuration status
	 * - Bit 1: Switch (SW) configuration status
	 * - Bit 2: Switch-End System (SW-ES) configuration status
	 *
	 * Each bit value of '1' indicates that the corresponding configuration is complete.
	 */
	uint8_t A664_BSP_CONFIG_STATUS;

	/**
	 * @brief ID of the PTP configuration.
	 */
	uint16_t A664_PTP_CONFIG_ID;

	/**
	 * @brief PTP device type. 0: Slave, 1: Master, 3: Grand Master.
	 */
	uint8_t A664_PTP_DEVICE_TYPE;

	/**
	 * @brief Status of PTP Rate-Correction. 0: disabled, 1: enabled.
	 */
	uint8_t A664_PTP_RC_STATUS;

	/**
	 * @brief PTP synchronization status of DTN End System’s Port A. This parameter is valid for PTP Slave.
	 */
	uint8_t A664_PTP_PORT_A_SYNC;

	/**
	 * @brief PTP synchronization status of DTN End System’s Port A. This parameter is valid for PTP Slave.
	 */
	uint8_t A664_PTP_PORT_B_SYNC;

	/**
	 * @brief Synchronization VL ID of PTP Master device. This parameter is valid for PTP Master/PTP Grand-master devices.
	 */
	uint16_t A664_PTP_SYNC_VL_ID;

	/**
	 * @brief Request VL ID of PTP Slave device. This parameter is valid for PTP Slave.
	 */
	uint16_t A664_PTP_REQ_VL_ID;

	/**
	 * @brief Response VL ID of PTP Slave device. This parameter is valid for PTP Slave.
	 */
	uint16_t A664_PTP_RES_VL_ID;

	/**
	 * @brief This parameter indicates from which network the PTP TOD information is received.
	 */
	uint8_t A664_PTP_TOD_NETWORK;

	/**
	 * @brief Hardware temperature of DTN End System.
	 */
	uint32_t A664_ES_HW_TEMP;

	/**
	 * @brief Internal Voltage value of DTN End System.
	 */
	uint32_t A664_ES_HW_VCC_INT;

	/**
	 * @brief Assigned port speed of DTN ES. 2 --> 1000Mbps, 1 --> 100Mbps and 0 --> 10Mbps.
	 */
	uint64_t A664_ES_PORT_SPEED;

	/**
	 * @brief Status of DTN ES Port A. If connection is provided then 1 = ONLINE , 0 = NO_LINK.
	 */
	uint64_t A664_ES_PORT_A_STATUS;

	/**
	 * @brief Status of DTN ES Port B. If connection is provided then 1 = ONLINE , 0 = NO_LINK.
	 */
	uint64_t A664_ES_PORT_B_STATUS;

	/**
	 * @brief Incoming packet count from DTN Driver on DTN End System TX buffer.
	 */
	uint64_t A664_ES_TX_INCOMING_COUNT;

	/**
	 * @brief Sent packet count on DTN End System Port A.
	 */
	uint64_t A664_ES_TX_A_OUTGOING_COUNT;

	/**
	 * @brief Sent packet count on DTN End System Port B.
	 */
	uint64_t A664_ES_TX_B_OUTGOING_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System due to VL ID error.
	 */
	uint64_t A664_ES_TX_VLID_DROP_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System due to minimum/maximum length error in TX direction.
	 */
	uint64_t A664_ES_TX_LMIN_LMAX_DROP_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System TX buffer due to packet delay exceeding maximum jitter.
	 */
	uint64_t A664_ES_TX_MAX_JITTER_DROP_COUNT;

	/**
	 * @brief Received packet count on DTN End System Port A.
	 */
	uint64_t A664_ES_RX_A_INCOMING_COUNT;

	/**
	 * @brief Received packet count on DTN End System Port B.
	 */
	uint64_t A664_ES_RX_B_INCOMING_COUNT;

	/**
	 * @brief Sent packet count to DTN Driver from DTN End System.
	 */
	uint64_t A664_ES_RX_OUTGOING_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port A due to VL ID error in RX direction.
	 */
	uint64_t A664_ES_RX_A_VLID_DROP_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port A due to minimum/maximum length error in RX direction.
	 */
	uint64_t A664_ES_RX_A_LMIN_LMAX_DROP_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port A due to network type error in RX direction.
	 */
	uint64_t A664_ES_RX_A_NET_ERR_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port A due to sequence number error in RX direction.
	 */
	uint64_t A664_ES_RX_A_SEQ_ERR_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port A due to CRC error in RX direction.
	 */
	uint64_t A664_ES_RX_A_CRC_ERROR_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port A due to IP checksum error in RX direction.
	 */
	uint64_t A664_ES_RX_A_IP_CHECKSUM_ERROR_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port B due to VL ID error in RX direction.
	 */
	uint64_t A664_ES_RX_B_VLID_DROP_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port B due to minimum/maximum length error in RX direction.
	 */
	uint64_t A664_ES_RX_B_LMIN_LMAX_DROP_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port B due to sequence number error in RX direction.
	 */
	uint64_t A664_ES_RX_B_SEQ_ERR_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port B due to network type error in RX direction.
	 */
	uint64_t A664_ES_RX_B_NET_ERR_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port B due to CRC error in RX direction.
	 */
	uint64_t A664_ES_RX_B_CRC_ERROR_COUNT;

	/**
	 * @brief Dropped packet count on DTN End System Port B due to IP checksum error in RX direction.
	 */
	uint64_t A664_ES_RX_B_IP_CHECKSUM_ERROR_COUNT;

	/**
	 * @brief Sent packet count from DTN Driver to DTN End System.
	 */
	uint64_t A664_BSP_TX_PACKET_COUNT;

	/**
	 * @brief Sent byte count from DTN Driver to DTN End System.
	 */
	uint64_t A664_BSP_TX_BYTE_COUNT;

	/**
	 * @brief Number of errors when sending packets from DTN Driver.
	 */
	uint64_t A664_BSP_TX_ERROR_COUNT;

	/**
	 * @brief Number of packets read by DTN Driver from DTN End System.
	 */
	uint64_t A664_BSP_RX_PACKET_COUNT;

	/**
	 * @brief Number of bytes received by DTN Driver from DTN End System.
	 */
	uint64_t A664_BSP_RX_BYTE_COUNT;

	/**
	 * @brief Number of errors when reading packets from DTN End System by DTN Driver.
	 */
	uint64_t A664_BSP_RX_ERROR_COUNT;

	/**
	 * @brief Number of packets dropped due to errors when reading packets from DTN End System by DTN Driver.
	 */
	uint64_t A664_BSP_RX_MISSED_FRAME_COUNT;

	/**
	 * @brief DTN Driver version number.
	 */
	uint64_t A664_BSP_VER;

	/**
	 * @brief This field indicates the hardware vendor of the DTN ES Device.
	 */
	uint64_t A664_ES_VENDOR_TYPE;

	/**
	 * @brief Number of packets dropped from the DTN Driver VL port buffers due to errors when reading packets from DTN End System by DTN Driver.
	 */
	uint64_t A664_ES_BSP_QUEUING_RX_VL_PORT_DROP_COUNT;

	/**
	 * @brief This parameter indicates the End System monitoring source. TRUE for Switch-End System, FALSE for End System.
	 */
	bool_gt A664_SW_ES_ENABLE;
} tA664ESMonitoring;

#endif /* CMC_ES_MONITORING_H */