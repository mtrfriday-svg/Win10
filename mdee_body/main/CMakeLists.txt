idf_component_register(SRCS "main.cc" "mdee_robot.cc" "link.cc"
                       INCLUDE_DIRS "." ".."
                       PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_uart esp_driver_i2c
                                     esp_adc esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>)
