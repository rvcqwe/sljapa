/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <helper/command.h>
#include <helper/binarybuffer.h>
#include <helper/time_support.h>
#include <helper/log.h>
#include <jtag/jtag.h>
#include <transport/transport.h>
#include <target/target.h>
#include "newchip_had.h"

int newchip_xfer(struct jtag_tap *tap, uint32_t instruction,
        uint32_t bits, uint64_t tx, uint64_t *rx)
{
    if (!transport_is_jtag() || !tap || !tap->enabled || !rx)
        return ERROR_FAIL;
    if (!tap->ir_length || tap->ir_length > 32 || !bits || bits > 64)
        return ERROR_COMMAND_ARGUMENT_INVALID;
    if ((tap->ir_length < 32 && (instruction >> tap->ir_length)) ||
            (bits < 64 && (tx >> bits)))
        return ERROR_COMMAND_ARGUMENT_INVALID;
    uint8_t ir_buf[4] = {0}, tx_buf[8] = {0}, rx_buf[8] = {0};
    buf_set_u32(ir_buf, 0, tap->ir_length, instruction);
    buf_set_u64(tx_buf, 0, bits, tx);
    struct scan_field ir = {
        .num_bits = tap->ir_length, .out_value = ir_buf,
    };
    struct scan_field dr = {
        .num_bits = bits, .out_value = tx_buf, .in_value = rx_buf,
    };
    jtag_add_ir_scan(tap, &ir, TAP_IDLE);
    jtag_add_dr_scan(tap, 1, &dr, TAP_IDLE);
    int retval = jtag_execute_queue();
    if (retval == ERROR_OK)
        *rx = buf_get_u64(rx_buf, 0, bits);
    return retval;
}

/* Bank 0 plain register accesses only; injected instructions need GO/EX. */
int newchip_had_access(struct jtag_tap *tap, uint32_t core, uint32_t reg,
        bool read, uint32_t bits, uint64_t tx, uint64_t *rx)
{
    if (!tap || tap->ir_length != 16 || core > 3 || reg > 31)
        return ERROR_COMMAND_ARGUMENT_INVALID;
    uint32_t instruction = (read ? 0x8000U : 0) | (reg << 8) | core;
    return newchip_xfer(tap, instruction, bits, tx, rx);
}

int newchip_had_status(struct jtag_tap *tap, uint32_t core, uint64_t *hsr)
{
    return newchip_had_access(tap, core, HAD_REG_HSR, true, 32, 0, hsr);
}

bool newchip_had_is_halted(uint64_t hsr)
{
    return (hsr & HAD_HSR_PM_MASK) == HAD_HSR_PM_DEBUG;
}

int newchip_had_halt(struct jtag_tap *tap, uint32_t core, uint64_t *last_hsr)
{
    uint64_t hcr, ignored;
    *last_hsr = 0;
    int retval = newchip_had_status(tap, core, last_hsr);
    if (retval != ERROR_OK || newchip_had_is_halted(*last_hsr))
        return retval;
    retval = newchip_had_access(tap, core, HAD_REG_HCR, true, 32, 0, &hcr);
    if (retval != ERROR_OK)
        return retval;
    retval = newchip_had_access(tap, core, HAD_REG_HCR,
            false, 32, hcr | HAD_HCR_DR, &ignored);
    if (retval != ERROR_OK)
        return retval;
    int64_t deadline = timeval_ms() + HAD_HALT_TIMEOUT_MS;
    for (;;) {
        keep_alive();
        retval = newchip_had_status(tap, core, last_hsr);
        if (retval != ERROR_OK)
            return retval;
        if (newchip_had_is_halted(*last_hsr))
            return ERROR_OK;
        if (timeval_ms() >= deadline)
            return ERROR_TIMEOUT_REACHED;
        alive_sleep(10);
    }
}

int newchip_had_resume(struct jtag_tap *tap, uint32_t core)
{
    uint64_t hsr, hcr, ignored;
    int retval = newchip_had_status(tap, core, &hsr);
    if (retval != ERROR_OK)
        return retval;
    if (!newchip_had_is_halted(hsr)) {
        LOG_ERROR("Resume requires a halted core (HSR.PM=2)");
        return ERROR_TARGET_NOT_HALTED;
    }
    retval = newchip_had_access(tap, core, HAD_REG_HCR, true, 32, 0, &hcr);
    if (retval != ERROR_OK)
        return retval;
    /* Preserve TME, breakpoint settings and every other HCR bit. */
    retval = newchip_had_access(tap, core, HAD_REG_HCR,
            false, 32, hcr & ~(uint64_t)HAD_HCR_DR, &ignored);
    if (retval != ERROR_OK)
        return retval;
    /* Reference c910_had_resume_core: bank 0 BYPASS, GO|EX, DR64 zeros. */
    uint32_t instruction = (HAD_REG_BYPASS << 8) | HAD_HACR_GO | HAD_HACR_EX | core;
    return newchip_xfer(tap, instruction, 64, 0, &ignored);
}

int newchip_had_pc(struct jtag_tap *tap, uint32_t core,
        bool write, uint64_t requested, uint64_t *value)
{
    int retval;
    if (write) {
        uint64_t hsr, ignored;
        retval = newchip_had_status(tap, core, &hsr);
        if (retval != ERROR_OK)
            return retval;
        if (!newchip_had_is_halted(hsr)) {
            LOG_ERROR("Halt core before writing PC");
            return ERROR_TARGET_NOT_HALTED;
        }
        retval = newchip_had_access(tap, core, HAD_REG_PC,
                false, 64, requested, &ignored);
        if (retval != ERROR_OK)
            return retval;
    }
    retval = newchip_had_access(tap, core, HAD_REG_PC, true, 64, 0, value);
    if (retval != ERROR_OK)
        return retval;
    if (write && *value != requested) {
        LOG_ERROR("PC readback mismatch");
        return ERROR_FAIL;
    }
    return ERROR_OK;
}

