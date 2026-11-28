idf_component_register(
    SRCS
        "env_chain.c"
        "env_chain_uart.c"
    INCLUDE_DIRS "."
    REQUIRES furi driver
)
