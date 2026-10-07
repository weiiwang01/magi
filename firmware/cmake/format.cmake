find_program(CLANG_FORMAT NAMES clang-format REQUIRED)
get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(GLOB_RECURSE FORMAT_SOURCES
    "${PROJECT_DIR}/Core/*.c"
    "${PROJECT_DIR}/Core/*.h"
    "${PROJECT_DIR}/Drivers/ch390-lwip/*.c"
    "${PROJECT_DIR}/Drivers/ch390-lwip/*.h"
)
list(REMOVE_ITEM FORMAT_SOURCES
    "${PROJECT_DIR}/Core/Inc/board.h"
    "${PROJECT_DIR}/Core/Inc/arch/cc.h"
    "${PROJECT_DIR}/Core/Inc/tiny_printf.h"
    "${PROJECT_DIR}/Core/Src/tiny_printf.c"
    "${PROJECT_DIR}/Core/Inc/user_settings.h"
    "${PROJECT_DIR}/Core/Inc/FreeRTOSConfig.h"
    "${PROJECT_DIR}/Core/Inc/lwipopts.h"
    "${PROJECT_DIR}/Core/Inc/stm32g0xx_hal_conf.h"
    "${PROJECT_DIR}/Core/Inc/main.h"
    "${PROJECT_DIR}/Core/Inc/stm32_assert.h"
    "${PROJECT_DIR}/Core/Inc/stm32g0xx_it.h"
    "${PROJECT_DIR}/Core/Src/app_freertos.c"
    "${PROJECT_DIR}/Core/Src/main.c"
    "${PROJECT_DIR}/Core/Src/stm32g0xx_hal_msp.c"
    "${PROJECT_DIR}/Core/Src/stm32g0xx_hal_timebase_tim.c"
    "${PROJECT_DIR}/Core/Src/stm32g0xx_it.c"
    "${PROJECT_DIR}/Core/Src/syscalls.c"
    "${PROJECT_DIR}/Core/Src/sysmem.c"
    "${PROJECT_DIR}/Core/Src/system_stm32g0xx.c"
)

execute_process(
    COMMAND "${CLANG_FORMAT}" --style=file --fallback-style=none -i ${FORMAT_SOURCES}
    WORKING_DIRECTORY "${PROJECT_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)
