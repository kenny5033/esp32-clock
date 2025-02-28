idf_component_register(
    SRCS "main.c" 
    INCLUDE_DIRS "." 
    REQUIRES tm1637 esp_wifi nvs_flash wpa_supplicant
)