enum {
    HAD_REG_ID  = 2,
    HAD_REG_HCR = 13,
    HAD_REG_HSR = 14,
    HAD_REG_PC  = 19,
};

#define HAD_HCR_DR (UINT32_C(1) << 15)

/* HAD bank 0: read/write, register index, core selection. */
static int newchip_had_access(struct jtag_tap *tap,
        unsigned int core, unsigned int reg,
        bool read, unsigned int bits,
        uint64_t tx, uint64_t *rx)
{
    uint32_t instruction =
        (read ? UINT32_C(0x8000) : 0) |
        (reg << 8) |
        core;

    return newchip_xfer(tap, instruction, bits, tx, rx);
}

COMMAND_HANDLER(handle_newchip_had)
{
    bool is_pc = strcmp(CMD_NAME, "pc") == 0;

    if (CMD_ARGC != 2 && !(is_pc && CMD_ARGC == 3))
        return ERROR_COMMAND_SYNTAX_ERROR;

    if (!transport_is_jtag())
        return ERROR_FAIL;

    struct jtag_tap *tap = jtag_tap_by_string(CMD_ARGV[0]);
    if (!tap || !tap->enabled) {
        command_print(CMD, "TAP not found or disabled");
        return ERROR_FAIL;
    }

    if (tap->ir_length != 16) {
        command_print(CMD, "C910 HAD requires a 16-bit IR");
        return ERROR_COMMAND_ARGUMENT_INVALID;
    }

    unsigned int core;
    COMMAND_PARSE_NUMBER(uint, CMD_ARGV[1], core);
    if (core > 3) {
        command_print(CMD, "Core must be 0..3; it must exist in the model");
        return ERROR_COMMAND_ARGUMENT_INVALID;
    }

    uint64_t value = 0;
    uint64_t ignored = 0;
    int retval;

    if (strcmp(CMD_NAME, "id") == 0) {
        retval = newchip_had_access(tap, core, HAD_REG_ID,
                true, 32, 0, &value);
        if (retval != ERROR_OK)
            return retval;

        command_print(CMD, "core %u ID = 0x%08" PRIx32,
                core, (uint32_t)value);
        return ERROR_OK;
    }

    if (strcmp(CMD_NAME, "status") == 0) {
        retval = newchip_had_access(tap, core, HAD_REG_HSR,
                true, 32, 0, &value);
        if (retval != ERROR_OK)
            return retval;

        command_print(CMD, "core %u HSR = 0x%08" PRIx32,
                core, (uint32_t)value);
        return ERROR_OK;
    }

    if (is_pc) {
        if (CMD_ARGC == 3) {
            uint64_t requested;
            COMMAND_PARSE_NUMBER(u64, CMD_ARGV[2], requested);

            retval = newchip_had_access(tap, core, HAD_REG_PC,
                    false, 64, requested, &ignored);
            if (retval != ERROR_OK)
                return retval;

            retval = newchip_had_access(tap, core, HAD_REG_PC,
                    true, 64, 0, &value);
            if (retval != ERROR_OK)
                return retval;

            if (value != requested) {
                command_print(CMD,
                        "PC mismatch: wrote 0x%016" PRIx64
                        ", read 0x%016" PRIx64,
                        requested, value);
                return ERROR_FAIL;
            }
        } else {
            retval = newchip_had_access(tap, core, HAD_REG_PC,
                    true, 64, 0, &value);
            if (retval != ERROR_OK)
                return retval;
        }

        command_print(CMD, "core %u PC = 0x%016" PRIx64,
                core, value);
        return ERROR_OK;
    }

    if (strcmp(CMD_NAME, "halt") == 0) {
        /* Preserve all existing HCR settings. */
        retval = newchip_had_access(tap, core, HAD_REG_HCR,
                true, 32, 0, &value);
        if (retval != ERROR_OK)
            return retval;

        value |= HAD_HCR_DR;

        retval = newchip_had_access(tap, core, HAD_REG_HCR,
                false, 32, value, &ignored);
        if (retval != ERROR_OK)
            return retval;

        uint64_t hcr, hsr;

        retval = newchip_had_access(tap, core, HAD_REG_HCR,
                true, 32, 0, &hcr);
        if (retval != ERROR_OK)
            return retval;

        retval = newchip_had_access(tap, core, HAD_REG_HSR,
                true, 32, 0, &hsr);
        if (retval != ERROR_OK)
            return retval;

        command_print(CMD,
                "core %u HCR = 0x%08" PRIx32
                ", HSR = 0x%08" PRIx32,
                core, (uint32_t)hcr, (uint32_t)hsr);

        if (!(hcr & HAD_HCR_DR)) {
            command_print(CMD, "Halt request bit did not read back");
            return ERROR_FAIL;
        }

        command_print(CMD,
                "Halt request set; halted state is not yet verified");
        return ERROR_OK;
    }

    return ERROR_COMMAND_SYNTAX_ERROR;
}

static const struct command_registration newchip_had_commands[] = {
    {
        .name = "id",
        .handler = handle_newchip_had,
        .mode = COMMAND_EXEC,
        .help = "Read C910 HAD ID.",
        .usage = "tap core",
    },
    {
        .name = "status",
        .handler = handle_newchip_had,
        .mode = COMMAND_EXEC,
        .help = "Read raw C910 HAD HSR.",
        .usage = "tap core",
    },
    {
        .name = "halt",
        .handler = handle_newchip_had,
        .mode = COMMAND_EXEC,
        .help = "Set halt request and read HCR/HSR; no halt wait yet.",
        .usage = "tap core",
    },
    {
        .name = "pc",
        .handler = handle_newchip_had,
        .mode = COMMAND_EXEC,
        .help = "Read PC, or write and verify PC. Write while halted.",
        .usage = "tap core [value]",
    },
    COMMAND_REGISTRATION_DONE
};