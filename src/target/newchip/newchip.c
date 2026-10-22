/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <inttypes.h>
#include <string.h>
#include <helper/command.h>
#include <jtag/jtag.h>
#include "newchip.h"
#include "newchip_had.h"

COMMAND_HANDLER(handle_newchip_xfer)
{
    if (CMD_ARGC != 4)
        return ERROR_COMMAND_SYNTAX_ERROR;
    struct jtag_tap *tap = jtag_tap_by_string(CMD_ARGV[0]);
    uint32_t instruction, bits;
    uint64_t tx, rx;
    COMMAND_PARSE_NUMBER(u32, CMD_ARGV[1], instruction);
    COMMAND_PARSE_NUMBER(u32, CMD_ARGV[2], bits);
    COMMAND_PARSE_NUMBER(u64, CMD_ARGV[3], tx);
    int retval = newchip_xfer(tap, instruction, bits, tx, &rx);
    if (retval == ERROR_OK)
        command_print(CMD, "rx = 0x%016" PRIx64, rx);
    return retval;
}

COMMAND_HANDLER(handle_newchip_had)
{
    bool is_pc = strcmp(CMD_NAME, "pc") == 0;
    if (CMD_ARGC != 2 && !(is_pc && CMD_ARGC == 3))
        return ERROR_COMMAND_SYNTAX_ERROR;
    struct jtag_tap *tap = jtag_tap_by_string(CMD_ARGV[0]);
    if (!tap || !tap->enabled || tap->ir_length != 16) {
        command_print(CMD, "Expected an enabled C910 TAP with 16-bit IR");
        return ERROR_COMMAND_ARGUMENT_INVALID;
    }
    uint32_t core;
    COMMAND_PARSE_NUMBER(u32, CMD_ARGV[1], core);
    if (core > 3)
        return ERROR_COMMAND_ARGUMENT_INVALID;
    uint64_t value = 0;
    int retval;
    if (strcmp(CMD_NAME, "id") == 0) {
        retval = newchip_had_access(tap, core, HAD_REG_ID, true, 32, 0, &value);
        if (retval == ERROR_OK)
            command_print(CMD, "core %u ID = 0x%08" PRIx32, core, (uint32_t)value);
    } else if (strcmp(CMD_NAME, "status") == 0) {
        retval = newchip_had_status(tap, core, &value);
        if (retval == ERROR_OK)
            command_print(CMD, "core %u HSR = 0x%08" PRIx32, core, (uint32_t)value);
    } else if (strcmp(CMD_NAME, "halt") == 0) {
        retval = newchip_had_halt(tap, core, &value);
        if (retval == ERROR_OK)
            command_print(CMD, "core %u halted: HSR = 0x%08" PRIx32,
                    core, (uint32_t)value);
        else if (retval == ERROR_TIMEOUT_REACHED)
            command_print(CMD, "core %u halt timeout: HSR = 0x%08" PRIx32,
                    core, (uint32_t)value);
    } else if (is_pc) {
        uint64_t requested = 0;
        if (CMD_ARGC == 3) {
            COMMAND_PARSE_NUMBER(u64, CMD_ARGV[2], requested);
        }
        retval = newchip_had_pc(tap, core, CMD_ARGC == 3, requested, &value);
        if (retval == ERROR_OK)
            command_print(CMD, "core %u PC = 0x%016" PRIx64, core, value);
    } else {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }
    return retval;
}

static const struct command_registration newchip_had_commands[] = {
    { .name = "id", .handler = handle_newchip_had, .mode = COMMAND_EXEC,
      .help = "Read C910 HAD ID.", .usage = "tap core" },
    { .name = "status", .handler = handle_newchip_had, .mode = COMMAND_EXEC,
      .help = "Read raw C910 HAD HSR.", .usage = "tap core" },
    { .name = "halt", .handler = handle_newchip_had, .mode = COMMAND_EXEC,
      .help = "Request halt and wait for debug mode.", .usage = "tap core" },
    { .name = "pc", .handler = handle_newchip_had, .mode = COMMAND_EXEC,
      .help = "Read PC, or write and verify PC while halted.",
      .usage = "tap core [value]" },
    COMMAND_REGISTRATION_DONE
};
static const struct command_registration newchip_commands[] = {
    { .name = "newchip_xfer", .handler = handle_newchip_xfer, .mode = COMMAND_EXEC,
      .help = "Raw IR/DR exchange; IR up to 32 bits, DR up to 64 bits.",
      .usage = "tap ir dr_bits tx" },
    { .name = "newchip", .mode = COMMAND_ANY, .help = "C910 HAD commands.",
      .chain = newchip_had_commands },
    COMMAND_REGISTRATION_DONE
};
int newchip_register_commands(struct command_context *cmd_ctx)
{
    return register_commands(cmd_ctx, NULL, newchip_commands);
}
