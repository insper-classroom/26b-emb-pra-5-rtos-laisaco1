set(PICO_SDK_FREERTOS_SOURCE FreeRTOS-Kernel)
set(FREERTOS_PORT ${PICO_SDK_FREERTOS_SOURCE}/portable/GCC/ARM_CM33_NTZ/non_secure)

add_library(freertos
    ${PICO_SDK_FREERTOS_SOURCE}/event_groups.c
    ${PICO_SDK_FREERTOS_SOURCE}/list.c
    ${PICO_SDK_FREERTOS_SOURCE}/queue.c
    ${PICO_SDK_FREERTOS_SOURCE}/stream_buffer.c
    ${PICO_SDK_FREERTOS_SOURCE}/tasks.c
    ${PICO_SDK_FREERTOS_SOURCE}/timers.c
    ${PICO_SDK_FREERTOS_SOURCE}/portable/MemMang/heap_3.c
    ${FREERTOS_PORT}/port.c
    ${FREERTOS_PORT}/portasm.c
)

target_include_directories(freertos PUBLIC
    .
    ${PICO_SDK_FREERTOS_SOURCE}/include
    ${FREERTOS_PORT}
)

target_link_libraries(freertos pico_stdlib)