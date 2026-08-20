/*
 * HID report descriptor dumper - see hid_dump.h
 *
 * Descriptor format reference: USB Device Class Definition for HID 1.11, s6.2.2.
 *
 * Short item prefix byte:
 *   bits 1:0  bSize   (0,1,2,3 -> 0,1,2,4 data bytes)
 *   bits 3:2  bType   (0=Main, 1=Global, 2=Local, 3=Reserved)
 *   bits 7:4  bTag
 * A prefix of 0xFE introduces a long item (bDataSize, bLongItemTag, data...).
 * Long items are not used in practice; we skip them but keep parsing aligned.
 */

#include "hid_dump.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Item types */
#define ITEM_TYPE_MAIN   0
#define ITEM_TYPE_GLOBAL 1
#define ITEM_TYPE_LOCAL  2

/* Main item tags */
#define TAG_MAIN_INPUT          0x8
#define TAG_MAIN_OUTPUT         0x9
#define TAG_MAIN_FEATURE        0xB
#define TAG_MAIN_COLLECTION     0xA
#define TAG_MAIN_END_COLLECTION 0xC

/* Global item tags */
#define TAG_GLOBAL_USAGE_PAGE   0x0
#define TAG_GLOBAL_LOGICAL_MIN  0x1
#define TAG_GLOBAL_LOGICAL_MAX  0x2
#define TAG_GLOBAL_PHYSICAL_MIN 0x3
#define TAG_GLOBAL_PHYSICAL_MAX 0x4
#define TAG_GLOBAL_UNIT_EXP     0x5
#define TAG_GLOBAL_UNIT         0x6
#define TAG_GLOBAL_REPORT_SIZE  0x7
#define TAG_GLOBAL_REPORT_ID    0x8
#define TAG_GLOBAL_REPORT_COUNT 0x9
#define TAG_GLOBAL_PUSH         0xA
#define TAG_GLOBAL_POP          0xB

/* Local item tags */
#define TAG_LOCAL_USAGE         0x0
#define TAG_LOCAL_USAGE_MIN     0x1
#define TAG_LOCAL_USAGE_MAX     0x2

#define MAX_TRACKED_REPORTS 24

typedef struct {
    uint16_t id;         /* 0 means "descriptor uses no Report IDs" */
    uint32_t in_bits;
    uint32_t out_bits;
    uint32_t feat_bits;
} report_acc_t;

/* --------------------------------------------------------------------------
 * Name tables
 * -------------------------------------------------------------------------- */

static const char *usage_page_name(uint32_t up)
{
    switch (up) {
    case 0x01: return "Generic Desktop";
    case 0x02: return "Simulation";
    case 0x03: return "VR";
    case 0x04: return "Sport";
    case 0x05: return "Game";
    case 0x06: return "Generic Device";
    case 0x07: return "Keyboard/Keypad";
    case 0x08: return "LED";
    case 0x09: return "Button";
    case 0x0A: return "Ordinal";
    case 0x0B: return "Telephony";
    case 0x0C: return "Consumer";
    case 0x0D: return "Digitizer";
    case 0x0F: return "PID";
    case 0x14: return "Alphanumeric Display";
    case 0x20: return "Sensor";
    case 0x84: return "Power Device";
    case 0x85: return "Battery System";
    default:
        if (up >= 0xFF00 && up <= 0xFFFF) return "Vendor-defined";
        return "Unknown";
    }
}

/* Usages on the Generic Desktop page that matter to us. */
static const char *generic_desktop_usage_name(uint32_t u)
{
    switch (u) {
    case 0x01: return "Pointer";
    case 0x02: return "Mouse";
    case 0x04: return "Joystick";
    case 0x05: return "Game Pad";
    case 0x06: return "Keyboard";
    case 0x07: return "Keypad";
    case 0x30: return "X";
    case 0x31: return "Y";
    case 0x38: return "Wheel";
    case 0x80: return "System Control";
    case 0x81: return "System Power Down";
    case 0x82: return "System Sleep";
    case 0x83: return "System Wake Up";
    default:   return NULL;
    }
}

static const char *collection_name(uint32_t c)
{
    switch (c) {
    case 0x00: return "Physical";
    case 0x01: return "Application";
    case 0x02: return "Logical";
    case 0x03: return "Report";
    case 0x04: return "Named Array";
    case 0x05: return "Usage Switch";
    case 0x06: return "Usage Modifier";
    default:   return "Vendor";
    }
}

/* Decode the Input/Output/Feature bit flags into something readable. */
static void main_item_flags(uint32_t v, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s,%s,%s%s%s%s",
             (v & 0x01) ? "Const" : "Data",
             (v & 0x02) ? "Var"   : "Array",
             (v & 0x04) ? "Rel"   : "Abs",
             (v & 0x08) ? ",Wrap" : "",
             (v & 0x10) ? ",NonLinear" : "",
             (v & 0x80) ? ",Volatile" : "");
}

/* --------------------------------------------------------------------------
 * Report accounting
 * -------------------------------------------------------------------------- */

static report_acc_t *find_or_add_report(report_acc_t *tbl, int *n, uint16_t id)
{
    for (int i = 0; i < *n; i++) {
        if (tbl[i].id == id) {
            return &tbl[i];
        }
    }
    if (*n >= MAX_TRACKED_REPORTS) {
        return NULL;
    }
    report_acc_t *r = &tbl[*n];
    memset(r, 0, sizeof(*r));
    r->id = id;
    (*n)++;
    return r;
}

/* Round bits up to whole bytes, then add the 1-byte Report ID prefix if used. */
static uint32_t report_bytes(uint32_t bits, bool has_report_id)
{
    uint32_t bytes = (bits + 7u) / 8u;
    return has_report_id ? bytes + 1u : bytes;
}

/* --------------------------------------------------------------------------
 * Public
 * -------------------------------------------------------------------------- */

void hid_dump_hex(const char *prefix, const uint8_t *data, size_t len)
{
    char line[3 * 16 + 1];
    for (size_t off = 0; off < len; off += 16) {
        size_t n = (len - off) < 16 ? (len - off) : 16;
        int p = 0;
        for (size_t i = 0; i < n; i++) {
            p += snprintf(line + p, sizeof(line) - p, "%02X ", data[off + i]);
        }
        printf("%s%04X: %s\n", prefix, (unsigned)off, line);
    }
}

void hid_dump_report_descriptor(const uint8_t *desc, size_t len, const char *label)
{
    if (desc == NULL || len == 0) {
        printf("[hid_dump] %s: no report descriptor\n", label);
        return;
    }

    printf("\n");
    printf("################################################################\n");
    printf("# HID REPORT DESCRIPTOR - %s  (%u bytes)\n", label, (unsigned)len);
    printf("################################################################\n");

    printf("\n--- raw ---\n");
    hid_dump_hex("  ", desc, len);

    printf("\n--- decoded ---\n");

    report_acc_t reports[MAX_TRACKED_REPORTS];
    int n_reports = 0;
    bool uses_report_ids = false;

    uint16_t cur_report_id = 0;
    uint32_t report_size = 0;
    uint32_t report_count = 0;
    uint32_t usage_page = 0;
    int indent = 0;

    size_t i = 0;
    while (i < len) {
        const uint8_t prefix = desc[i];

        /* Long item - skip, but stay aligned. */
        if (prefix == 0xFE) {
            uint8_t data_size = (i + 1 < len) ? desc[i + 1] : 0;
            printf("  %*s[long item, %u data bytes - skipped]\n", indent * 2, "", data_size);
            i += 3u + data_size;
            continue;
        }

        uint8_t b_size = prefix & 0x03;
        if (b_size == 3) {
            b_size = 4;
        }
        const uint8_t b_type = (prefix >> 2) & 0x03;
        const uint8_t b_tag = (prefix >> 4) & 0x0F;

        if (i + 1u + b_size > len) {
            printf("  !! truncated item at offset %u (prefix 0x%02X)\n", (unsigned)i, prefix);
            break;
        }

        /* Unsigned value */
        uint32_t val = 0;
        for (uint8_t k = 0; k < b_size; k++) {
            val |= ((uint32_t)desc[i + 1u + k]) << (8u * k);
        }
        /* Sign-extended value, for Logical/Physical Min/Max */
        int32_t sval = (int32_t)val;
        if (b_size == 1 && (val & 0x80u)) {
            sval = (int32_t)(val | 0xFFFFFF00u);
        } else if (b_size == 2 && (val & 0x8000u)) {
            sval = (int32_t)(val | 0xFFFF0000u);
        }

        /* Close collections before printing, so indentation reads naturally. */
        if (b_type == ITEM_TYPE_MAIN && b_tag == TAG_MAIN_END_COLLECTION && indent > 0) {
            indent--;
        }

        printf("  %*s", indent * 2, "");

        switch (b_type) {
        case ITEM_TYPE_MAIN:
            switch (b_tag) {
            case TAG_MAIN_INPUT:
            case TAG_MAIN_OUTPUT:
            case TAG_MAIN_FEATURE: {
                char flags[64];
                main_item_flags(val, flags, sizeof(flags));
                const char *kind = (b_tag == TAG_MAIN_INPUT)  ? "Input"
                                 : (b_tag == TAG_MAIN_OUTPUT) ? "Output"
                                                              : "Feature";
                const uint32_t bits = report_size * report_count;
                printf("%-8s (%s)   [%u x %u bits = %u bits]\n",
                       kind, flags, (unsigned)report_count, (unsigned)report_size, (unsigned)bits);

                report_acc_t *r = find_or_add_report(reports, &n_reports, cur_report_id);
                if (r != NULL) {
                    if (b_tag == TAG_MAIN_INPUT) {
                        r->in_bits += bits;
                    } else if (b_tag == TAG_MAIN_OUTPUT) {
                        r->out_bits += bits;
                    } else {
                        r->feat_bits += bits;
                    }
                }
                break;
            }
            case TAG_MAIN_COLLECTION:
                printf("Collection (%s)\n", collection_name(val));
                indent++;
                break;
            case TAG_MAIN_END_COLLECTION:
                printf("End Collection\n");
                break;
            default:
                printf("Main(tag 0x%X) = 0x%X\n", b_tag, (unsigned)val);
                break;
            }
            break;

        case ITEM_TYPE_GLOBAL:
            switch (b_tag) {
            case TAG_GLOBAL_USAGE_PAGE:
                usage_page = val;
                printf("Usage Page (%s) [0x%02X]\n", usage_page_name(val), (unsigned)val);
                break;
            case TAG_GLOBAL_LOGICAL_MIN:
                printf("Logical Minimum (%ld)\n", (long)sval);
                break;
            case TAG_GLOBAL_LOGICAL_MAX:
                printf("Logical Maximum (%ld)\n", (long)sval);
                break;
            case TAG_GLOBAL_PHYSICAL_MIN:
                printf("Physical Minimum (%ld)\n", (long)sval);
                break;
            case TAG_GLOBAL_PHYSICAL_MAX:
                printf("Physical Maximum (%ld)\n", (long)sval);
                break;
            case TAG_GLOBAL_UNIT_EXP:
                printf("Unit Exponent (%ld)\n", (long)sval);
                break;
            case TAG_GLOBAL_UNIT:
                printf("Unit (0x%X)\n", (unsigned)val);
                break;
            case TAG_GLOBAL_REPORT_SIZE:
                report_size = val;
                printf("Report Size (%u)\n", (unsigned)val);
                break;
            case TAG_GLOBAL_REPORT_ID:
                cur_report_id = (uint16_t)val;
                uses_report_ids = true;
                printf("Report ID (0x%02X)   <<<<\n", (unsigned)val);
                break;
            case TAG_GLOBAL_REPORT_COUNT:
                report_count = val;
                printf("Report Count (%u)\n", (unsigned)val);
                break;
            case TAG_GLOBAL_PUSH:
                printf("Push\n");
                break;
            case TAG_GLOBAL_POP:
                printf("Pop\n");
                break;
            default:
                printf("Global(tag 0x%X) = 0x%X\n", b_tag, (unsigned)val);
                break;
            }
            break;

        case ITEM_TYPE_LOCAL:
            switch (b_tag) {
            case TAG_LOCAL_USAGE: {
                const char *nm = (usage_page == 0x01) ? generic_desktop_usage_name(val) : NULL;
                if (nm != NULL) {
                    printf("Usage (%s) [0x%02X]\n", nm, (unsigned)val);
                } else {
                    printf("Usage (0x%02X)\n", (unsigned)val);
                }
                break;
            }
            case TAG_LOCAL_USAGE_MIN:
                printf("Usage Minimum (0x%02X)\n", (unsigned)val);
                break;
            case TAG_LOCAL_USAGE_MAX:
                printf("Usage Maximum (0x%02X)\n", (unsigned)val);
                break;
            default:
                printf("Local(tag 0x%X) = 0x%X\n", b_tag, (unsigned)val);
                break;
            }
            break;

        default:
            printf("Reserved(tag 0x%X, size %u) = 0x%X\n", b_tag, b_size, (unsigned)val);
            break;
        }

        i += 1u + b_size;
    }

    /* ---- summary ---------------------------------------------------------
     * This table is the direct input to the Phase 3 BLE report map: it lists
     * every report this interface can emit and exactly how many bytes each is
     * on the wire (including the Report ID prefix byte where applicable).
     * ------------------------------------------------------------------- */
    printf("\n--- report summary for %s ---\n", label);
    printf("  Uses Report IDs: %s\n", uses_report_ids ? "YES" : "NO (single unnumbered report)");
    printf("  %-12s %-14s %-14s %-14s\n", "Report ID", "Input bytes", "Output bytes", "Feature bytes");
    for (int r = 0; r < n_reports; r++) {
        char idbuf[12];
        if (uses_report_ids) {
            snprintf(idbuf, sizeof(idbuf), "0x%02X", reports[r].id);
        } else {
            snprintf(idbuf, sizeof(idbuf), "(none)");
        }
        printf("  %-12s %-14u %-14u %-14u\n",
               idbuf,
               (unsigned)(reports[r].in_bits   ? report_bytes(reports[r].in_bits,   uses_report_ids) : 0),
               (unsigned)(reports[r].out_bits  ? report_bytes(reports[r].out_bits,  uses_report_ids) : 0),
               (unsigned)(reports[r].feat_bits ? report_bytes(reports[r].feat_bits, uses_report_ids) : 0));
    }
    printf("################################################################\n\n");
}
