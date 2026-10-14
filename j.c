/* Настоящий JTAG-обмен. Пока ограничиваем IR и DR 32 битами. */
static int newchip_xfer(struct jtag_tap *tap,
        uint32_t instruction, unsigned int bits,
        uint32_t tx, uint32_t *rx)
{
    uint8_t ir_buf[4] = {0};
    uint8_t tx_buf[4] = {0};
    uint8_t rx_buf[4] = {0};

    buf_set_u32(ir_buf, 0, tap->ir_length, instruction);
    buf_set_u32(tx_buf, 0, bits, tx);

    struct scan_field ir = {
        .num_bits = tap->ir_length,
        .out_value = ir_buf,
        .in_value = NULL,
    };

    struct scan_field dr = {
        .num_bits = bits,
        .out_value = tx_buf,
        .in_value = rx_buf,
    };

    jtag_add_ir_scan(tap, &ir, TAP_IDLE);
    jtag_add_dr_scan(tap, 1, &dr, TAP_IDLE);

    int retval = jtag_execute_queue();
    if (retval != ERROR_OK)
        return retval;

    *rx = buf_get_u32(rx_buf, 0, bits);
    return ERROR_OK;
}

/* newchip_xfer <tap> <ir> <dr_bits> <tx> */
COMMAND_HANDLER(handle_newchip_xfer)
{
    if (CMD_ARGC != 4)
        return ERROR_COMMAND_SYNTAX_ERROR;

    if (!transport_is_jtag())
        return ERROR_FAIL;

    struct jtag_tap *tap = jtag_tap_by_string(CMD_ARGV[0]);
    if (!tap || !tap->enabled) {
        command_print(CMD, "TAP not found or disabled");
        return ERROR_FAIL;
    }

    uint32_t instruction, bits, tx;
    COMMAND_PARSE_NUMBER(u32, CMD_ARGV[1], instruction);
    COMMAND_PARSE_NUMBER(u32, CMD_ARGV[2], bits);
    COMMAND_PARSE_NUMBER(u32, CMD_ARGV[3], tx);

    if (tap->ir_length == 0 || tap->ir_length > 32 ||
            bits == 0 || bits > 32)
        return ERROR_COMMAND_ARGUMENT_INVALID;

    if (tap->ir_length < 32 &&
            (instruction >> tap->ir_length) != 0)
        return ERROR_COMMAND_ARGUMENT_INVALID;

    uint32_t rx;
    int retval = newchip_xfer(tap, instruction, bits, tx, &rx);
    if (retval == ERROR_OK)
        command_print(CMD, "rx = 0x%08" PRIx32, rx);

    return retval;
}