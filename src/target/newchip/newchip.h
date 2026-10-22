/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPENOCD_TARGET_NEWCHIP_H
#define OPENOCD_TARGET_NEWCHIP_H
struct command_context;
int newchip_register_commands(struct command_context *cmd_ctx);
#endif
