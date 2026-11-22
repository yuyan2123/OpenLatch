if(OUTDOOR_PN532_CARD_TEST)
    idf_component_register(SRCS "Pn532CardTest.cpp"
        PRIV_REQUIRES esp_driver_gpio pn532_hal pn532_cxx)
    target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
    return()
endif()

set(PRIVATE_INCLUDES "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../private")
    list(APPEND PRIVATE_INCLUDES "../../private")
endif()
idf_component_register(
    SRCS "main.cpp" "PnReader.cpp"
    INCLUDE_DIRS "." "../../include" ${PRIVATE_INCLUDES}
    PRIV_REQUIRES esp_driver_gpio esp_wifi esp_event esp_netif nvs_flash mbedtls esp_timer pn532_hal pn532_cxx)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
set(CREDENTIALS_HASH "unconfigured")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../private/credentials.h")
    file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../../private/credentials.h" CREDENTIALS_HASH)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE OUTDOOR_CREDENTIALS_HASH="${CREDENTIALS_HASH}")
