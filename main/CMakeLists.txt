idf_component_register(SRCS "salt_level_monitor.c"
                    PRIV_REQUIRES esp_wifi nvs_flash mqtt driver esp_timer
                    INCLUDE_DIRS ".")
