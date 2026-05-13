#ifndef FIRMWARE_UPDATER_DEFAULTS_H
#define FIRMWARE_UPDATER_DEFAULTS_H

#include <cstdint>
#include "UnitManager.h"

// ============================================================================
// FirmwareUpdater per-unit defaults (C++ side).
// ----------------------------------------------------------------------------
// Edit values here when a unit's LRU ID changes. The value is the on-wire
// big-endian word as packed by struct.pack('>H', lru_id) in fpga_firmware_
// loader.py — i.e. write the literal you want to see on the wire.
//
// Network defaults (interface, VLAN, BAG rate, etc.) live in the Python
// script: FirmwareUpdater/fpga_firmware_loader.py (DEFAULT_* constants).
// Each Python default is also overridable via a matching --<name> CLI flag.
// ============================================================================

namespace FirmwareUpdaterDefaults
{

// Per-unit LRU ID. Placeholder 0x0001 for units not yet brought up.
inline uint16_t lruIdForUnit(Unit unit)
{
    switch (unit)
    {
    case DTN: return 0x2600;
    case VMC: return 0x1B00;
    case CMC: return 0x0001;  // placeholder
    case MMC: return 0x0001;  // placeholder
    case HSN: return 0x0001;  // placeholder
    }
    return 0x0001;
}

} // namespace FirmwareUpdaterDefaults

#endif // FIRMWARE_UPDATER_DEFAULTS_H
