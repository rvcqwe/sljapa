/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPENOCD_TARGET_NEWCHIP_HAD_H
#define OPENOCD_TARGET_NEWCHIP_HAD_H
#include <stdbool.h>
#include <stdint.h>
struct jtag_tap;
enum {
    HAD_REG_ID = 2, HAD_REG_HCR = 13, HAD_REG_HSR = 14, HAD_REG_PC = 19,
};
#define HAD_ID_EXPECTED 0x080d2b43U
#define HAD_HCR_DR (1U << 15)
#define HAD_HSR_PM_MASK 3U
#define HAD_HSR_PM_DEBUG 2U
#define HAD_HALT_TIMEOUT_MS 30000
int newchip_xfer(struct jtag_tap *tap, uint32_t instruction,
        uint32_t bits, uint64_t tx, uint64_t *rx);
int newchip_had_access(struct jtag_tap *tap, uint32_t core, uint32_t reg,
        bool read, uint32_t bits, uint64_t tx, uint64_t *rx);
int newchip_had_status(struct jtag_tap *tap, uint32_t core, uint64_t *hsr);
bool newchip_had_is_halted(uint64_t hsr);
int newchip_had_halt(struct jtag_tap *tap, uint32_t core, uint64_t *last_hsr);
int newchip_had_pc(struct jtag_tap *tap, uint32_t core,
        bool write, uint64_t requested, uint64_t *value);
#endif
