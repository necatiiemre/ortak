#ifndef CMC_OTHER_TYPES_H
#define CMC_OTHER_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SRC_MAX_CONN 20
#define DST_MAX_CONN 8

typedef struct __attribute__((packed))
{
	uint64_t send_count[SRC_MAX_CONN];
	uint64_t send_fail_count[SRC_MAX_CONN];
	uint64_t receive_count[SRC_MAX_CONN];
	uint64_t crc_pass_count[SRC_MAX_CONN];
	uint64_t crc_fail_count[SRC_MAX_CONN];
	uint64_t pkg_drop_count[SRC_MAX_CONN];
} COUNTERS_DPM;

typedef struct __attribute__((packed))
{
	uint64_t send_count[DST_MAX_CONN];
	uint64_t send_fail_count[DST_MAX_CONN];
	uint64_t receive_count[DST_MAX_CONN];
	uint64_t crc_pass_count[DST_MAX_CONN];
	uint64_t crc_fail_count[DST_MAX_CONN];
	uint64_t pkg_drop_count[DST_MAX_CONN];
} COUNTERS_DSM;

/*inter lrm'in haberleşme countları — DPM ve DSM ayrı paketler olarak gelir*/


// NOT: Pcs_* tipleri wire formatında natural alignment kullanır (CMC firmware
// non-packed gönderiyor; toplam 136 byte). Bu nedenle bu struct'lar packed
// DEĞİL — Pcs_monitor_type 16 byte (1+7+8), Pcs_profile_stats 136 byte.
typedef struct Pcs_monitor_type
{
    uint8_t   percentage;                     /*!< Percentage */
    uint64_t  usage;                          /*!< Usage amount (memory or time) */
} Pcs_monitor_type;

typedef struct Pcs_cpu_exec_time_type
{
    Pcs_monitor_type min_exec_time;             /*!< Minimum execution time in nanosec */
    Pcs_monitor_type max_exec_time;             /*!< Maximum execution time in nanosec */
    Pcs_monitor_type avg_exec_time;             /*!< Average execution time in nanosec */
    Pcs_monitor_type last_exec_time;            /*!< Latest execution time in nanosec */
} Pcs_cpu_exec_time_type;

typedef struct Pcs_mem_profile_type
{
    size_t total_size;            /*!< Total memory size in bytes */
    size_t used_size;             /*!< Used memory size in bytes */
    size_t max_used_size;         /*!< Maximum used memory size in bytes */
} Pcs_mem_profile_type;

typedef struct Pcs_profile_stats
{
    uint64_t              sample_count;       /*!< Number of samples (number of majors for ARINC, number of monitoring windows for POSIX) used for this profiling statistic. */
    uint64_t              latest_read_time;   /*!< Timestamp for the last profiling statistics calculation (in nanoseconds).  */
    uint64_t              total_run_time;     /*!< Execution time + Idle time for a major frame (in nanosec). Only valid for the last measurement (last major frame). */
    Pcs_cpu_exec_time_type  cpu_exec_time;      /*!< CPU execution time details in nanosec. */
    Pcs_mem_profile_type    heap_mem;           /*!< Heap memory information. */
    Pcs_mem_profile_type    stack_mem;          /*!< Stack memory information. */
} Pcs_profile_stats;

#endif /* CMC_OTHER_TYPES_H */