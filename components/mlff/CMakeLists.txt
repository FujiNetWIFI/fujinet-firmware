idf_component_register(SRCS "mlff.c"
                    INCLUDE_DIRS .
                    PRIV_REQUIRES fatfs mbedtls nvs_flash spi_flash)
