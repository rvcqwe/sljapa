    {
        .name = "newchip_xfer",
        .handler = handle_newchip_xfer,
        .mode = COMMAND_EXEC,
        .help = "Execute raw IR/DR exchange; IR and DR up to 32 bits.",
        .usage = "tap ir dr_bits tx",
    },