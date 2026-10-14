/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>

#include <helper/command.h>
#include <helper/binarybuffer.h>
#include <jtag/jtag.h>
#include <transport/transport.h>

#include "newchip.h"

/*
 * Сюда перенеси из openocd.c обе функции целиком:
 *
 * static int newchip_xfer(...)
 * COMMAND_HANDLER(handle_newchip_xfer)
 */

static const struct command_registration newchip_commands[] = {
    {
        .name = "newchip_xfer",
        .handler = handle_newchip_xfer,
        .mode = COMMAND_EXEC,
        .help = "Execute raw IR/DR exchange; IR and DR up to 32 bits.",
        .usage = "tap ir dr_bits tx",
    },
    COMMAND_REGISTRATION_DONE
};

int newchip_register_commands(struct command_context *cmd_ctx)
{
    return register_commands(cmd_ctx, NULL, newchip_commands);
}