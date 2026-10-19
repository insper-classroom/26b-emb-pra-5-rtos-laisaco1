add_library(ssd1306_lib
    ssd1306.c
)

target_link_libraries(ssd1306_lib pico_stdlib hardware_i2c)

target_include_directories(ssd1306_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})