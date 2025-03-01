set(srcs "main.c")

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES tm1637 esp_wifi nvs_flash wpa_supplicant esp_http_server
    EMBED_TXTFILES html/index.html
)
