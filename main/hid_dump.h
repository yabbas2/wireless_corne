/*
 * HID report descriptor dumper.
 *
 * Phase 1 deliverable. Parses a raw USB HID report descriptor and prints:
 *   1. a raw hex dump (so it can be pasted / archived / re-parsed offline),
 *   2. a decoded, indented item stream,
 *   3. a per-Report-ID size summary.
 *
 * The size summary is the important part: it gives the exact input/output/
 * feature report lengths we must reproduce in the BLE report map in Phase 3.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse and pretty-print a HID report descriptor to the console.
 *
 * @param desc   Pointer to the raw report descriptor bytes.
 * @param len    Length of the descriptor in bytes.
 * @param label  Short human-readable label, e.g. "iface 0".
 */
void hid_dump_report_descriptor(const uint8_t *desc, size_t len, const char *label);

/**
 * @brief Hex-dump an arbitrary buffer (used for raw input reports).
 *
 * @param prefix  Line prefix.
 * @param data    Buffer.
 * @param len     Length in bytes.
 */
void hid_dump_hex(const char *prefix, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
