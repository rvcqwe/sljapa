/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <inttypes.h>
#include <helper/log.h>
#include <target/target.h>
#include <target/target_type.h>
#include <jtag/jtag.h>
#include <transport/transport.h>
#include "newchip_had.h"

static int c910_validate(struct target *target)
{
    if (!transport_is_jtag() || !target->tap ||
            !target->tap->enabled || target->tap->ir_length != 16 ||
            target->coreid < 0 || target->coreid > 3) {
        LOG_TARGET_ERROR(target, "Requires JTAG, enabled 16-bit TAP and coreid 0..3");
        return ERROR_FAIL;
    }
    return ERROR_OK;
}

static int c910_init(struct command_context *cmd_ctx, struct target *target)
{
    (void)cmd_ctx;
    target->state = TARGET_UNKNOWN;
    target->debug_reason = DBG_REASON_UNDEFINED;
    return c910_validate(target);
}

/* PM=3 is ambiguous (reset/idle), so do not claim TARGET_RESET. */
static int c910_apply_status(struct target *target, uint64_t hsr, bool notify)
{
    enum target_state previous = target->state;
    switch (hsr & HAD_HSR_PM_MASK) {
    case HAD_HSR_PM_DEBUG:
        target->state = TARGET_HALTED;
        /* DRO/ADRO are occurrence bits; not a reliable general stop cause. */
        if (previous != TARGET_HALTED)
            target->debug_reason = DBG_REASON_UNDEFINED;
        break;
    case 0: /* Active */
    case 1: /* LowPower: not debug-halted */
        target->state = TARGET_RUNNING;
        target->debug_reason = DBG_REASON_NOTHALTED;
        break;
    default:
        target->state = TARGET_UNKNOWN;
        target->debug_reason = DBG_REASON_UNDEFINED;
        break;
    }
    if (notify && previous != target->state) {
        if (target->state == TARGET_HALTED)
            return target_call_event_callbacks(target, TARGET_EVENT_HALTED);
        if (previous == TARGET_HALTED && target->state == TARGET_RUNNING)
            return target_call_event_callbacks(target, TARGET_EVENT_RESUMED);
    }
    return ERROR_OK;
}

static int c910_poll(struct target *target)
{
    int retval = c910_validate(target);
    if (retval != ERROR_OK)
        return retval;
    uint64_t hsr;
    retval = newchip_had_status(target->tap, target->coreid, &hsr);
    if (retval != ERROR_OK) {
        target->state = TARGET_UNKNOWN;
        return retval;
    }
    return c910_apply_status(target, hsr, true);
}

static int c910_examine(struct target *target)
{
    int retval = c910_validate(target);
    if (retval != ERROR_OK)
        return retval;
    uint64_t id, hsr;
    retval = newchip_had_access(target->tap, target->coreid,
            HAD_REG_ID, true, 32, 0, &id);
    if (retval != ERROR_OK)
        return retval;
    if (id != HAD_ID_EXPECTED) {
        LOG_TARGET_ERROR(target, "Unexpected HAD ID: 0x%08" PRIx32, (uint32_t)id);
        return ERROR_FAIL;
    }
    retval = newchip_had_status(target->tap, target->coreid, &hsr);
    if (retval != ERROR_OK)
        return retval;
    c910_apply_status(target, hsr, false);
    target_set_examined(target);
    LOG_TARGET_INFO(target, "C910 HAD core %d, ID=0x%08" PRIx32,
            target->coreid, (uint32_t)id);
    return ERROR_OK;
}

static int c910_halt(struct target *target)
{
    int retval = c910_validate(target);
    if (retval != ERROR_OK)
        return retval;
    /* Refresh even when cached state says halted: raw commands may change it. */
    retval = c910_poll(target);
    if (retval != ERROR_OK || target->state == TARGET_HALTED)
        return retval;
    uint64_t hsr = 0;
    retval = newchip_had_halt(target->tap, target->coreid, &hsr);
    if (retval != ERROR_OK) {
        target->state = TARGET_UNKNOWN;
        LOG_TARGET_ERROR(target, "Halt failed (%d), last HSR=0x%08" PRIx32,
                retval, (uint32_t)hsr);
        return retval;
    }
    target->state = TARGET_HALTED;
    target->debug_reason = DBG_REASON_DBGRQ;
    return target_call_event_callbacks(target, TARGET_EVENT_HALTED);
}

static int c910_arch_state(struct target *target)
{
    LOG_TARGET_USER(target, "C910 HAD core %d: %s", target->coreid,
            target_state_name(target));
    return ERROR_OK;
}

static int c910_resume(struct target *target, bool current, target_addr_t address,
        bool handle_breakpoints, bool debug_execution)
{
    (void)handle_breakpoints;
    if (debug_execution || target->breakpoints || target->watchpoints) {
        LOG_TARGET_ERROR(target, "Debug execution and break/watchpoints are not supported yet");
        return ERROR_NOT_IMPLEMENTED;
    }
    int retval = c910_poll(target);
    if (retval != ERROR_OK)
        return retval;
    if (target->state != TARGET_HALTED)
        return ERROR_TARGET_NOT_HALTED;
    if (!current) {
        if (address & 1) {
            LOG_TARGET_ERROR(target, "PC must be aligned to at least 2 bytes");
            return ERROR_COMMAND_ARGUMENT_INVALID;
        }
        uint64_t pc;
        retval = newchip_had_pc(target->tap, target->coreid, true, address, &pc);
        if (retval != ERROR_OK)
            return retval;
    }
    retval = newchip_had_resume(target->tap, target->coreid);
    if (retval != ERROR_OK) {
        /* A failed transfer may already have reached the model. */
        target->state = TARGET_UNKNOWN;
        target->debug_reason = DBG_REASON_UNDEFINED;
        return retval;
    }
    /* OpenOCD requires asynchronous resume. A later poll may see an immediate
     * re-halt; do not wait for PM=Active here or lose that transition. */
    target->state = TARGET_RUNNING;
    target->debug_reason = DBG_REASON_NOTHALTED;
    return target_call_event_callbacks(target, TARGET_EVENT_RESUMED);
}
static int c910_step(struct target *target, bool current, target_addr_t address,
        bool handle_breakpoints)
{
    (void)current; (void)address; (void)handle_breakpoints;
    LOG_TARGET_ERROR(target, "step is not implemented yet");
    return ERROR_NOT_IMPLEMENTED;
}

struct target_type c910_had_target = {
    .name = "c910_had",
    .init_target = c910_init,
    .examine = c910_examine,
    .poll = c910_poll,
    .halt = c910_halt,
    .arch_state = c910_arch_state,
    .resume = c910_resume,
    .step = c910_step,
    /* No get_gdb_reg_list: OpenOCD excludes this stage from GDB services. */
};

