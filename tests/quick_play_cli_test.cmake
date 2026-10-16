function(check_cli expected_code expected_text)
    execute_process(COMMAND "${PROGRAM}" ${ARGN}
        RESULT_VARIABLE code OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 5)
    if(NOT "${code}" STREQUAL "${expected_code}" OR
       NOT "${stdout}${stderr}" MATCHES "${expected_text}")
        message(FATAL_ERROR "Unexpected CLI result ${code}: ${stdout}${stderr}")
    endif()
endfunction()

check_cli(0 "--qp-continuous-cancel" --help)
check_cli(2 "Unexpected argument" --invalid-option)

file(MAKE_DIRECTORY "${WORK_DIRECTORY}")
file(WRITE "${WORK_DIRECTORY}/quick_play.ini"
    "[MODEL]\nModel = 3\n[GAME]\nSimulation PPS = 2.5\n[AUTOPLAY]\nAutoplay = 1\nPPS of Autoplay = 0\nWidth of Autoplay = 60\n")

function(check_live_run name enabled)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "TETR_USER_CONFIG=${WORK_DIRECTORY}/quick_play.ini"
        "TETR_USER_CONFIG_PLAYER2=${WORK_DIRECTORY}/quick_play.ini"
        TETR_TEST_BATCH=1 TETR_EXIT_AFTER_GAME=1 TETR_TEST_MAX_PIECES=8
        TETR_TEST_SEED=20261003 TETR_TEST_CLOCK_SCALE=100
        "TETR_TEST_REPLAY=${name}.txt"
        "${PROGRAM}" ${ARGN}
        WORKING_DIRECTORY "${WORK_DIRECTORY}"
        RESULT_VARIABLE code OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 20)
    if(NOT "${code}" STREQUAL "0")
        message(FATAL_ERROR "Quick Play ${name} failed (${code}): ${stdout}${stderr}")
    endif()
    file(READ "${WORK_DIRECTORY}/.tetr/replay/${name}.txt" replay)
    string(FIND "${replay}" "{QP_continuous_cancellation,${enabled}}" option_position)
    if(option_position LESS 0 OR NOT replay MATCHES "\\{end_place_data,")
        message(FATAL_ERROR "Quick Play ${name} did not record the expected startup option/gameplay.")
    endif()
    string(FIND "${replay}" "{undo_count,0}" undo_position)
    if(undo_position LESS 0)
        message(FATAL_ERROR "Quick Play ${name} did not save its zero undo count.")
    endif()
endfunction()

if(WIN32)
    set(keybinding_suffix windows)
else()
    set(keybinding_suffix linux)
endif()
set(player_one_bindings "${SOURCE_DIRECTORY}/keybinding_${keybinding_suffix}.txt")
set(player_two_bindings "${SOURCE_DIRECTORY}/keybinding_player2_${keybinding_suffix}.txt")

check_live_run(default 0 "${player_one_bindings}" "${player_two_bindings}")
check_live_run(enabled_before 1 --qp-continuous-cancel
    "${player_one_bindings}" "${player_two_bindings}")
check_live_run(enabled_after 1 "${player_one_bindings}"
    "${player_two_bindings}" --qp-continuous-cancel)
