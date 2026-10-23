cmake_minimum_required(VERSION 3.18)
project(tetr_terminal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Both clients must consume the same runtime/rules/presentation contracts.
option(BUILD_TESTING "Build the regression suite (opt in)" OFF)
option(TETR_BUILD_TERMINAL "Build the complete original terminal client and FT7 tools" ON)
option(TETR_BUILD_OPENGL "Build the OpenGL client (migration phase 2)" OFF)
option(TETR_BUILD_SERVER "Build the standalone relay server" ON)
if(BUILD_TESTING AND TETR_BUILD_TERMINAL AND NOT TETR_BUILD_SERVER)
    message(FATAL_ERROR "The full terminal integration suite requires TETR_BUILD_SERVER=ON. Headless foundation tests support both server settings.")
endif()

if(WIN32)
    # Apply to every target, including the upstream BOT libraries.
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
    # Game sessions and BOT search state exceed Windows' default stack reserve.
    if(MSVC)
        add_link_options(/STACK:16777216)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_link_options(-Wl,--stack,16777216)
    endif()
endif()
if(MSVC)
    # Enable standard C++ exception unwinding in every target and library.
    add_compile_options(/utf-8 /EHsc)
endif()

if(ANDROID)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
find_package(Threads REQUIRED)
include(cmake/PortableBuild.cmake)
include(cmake/WebSocketDependencies.cmake)
if(TETR_BUILD_TERMINAL OR TETR_BUILD_OPENGL OR TETR_BUILD_SERVER)
    tetr_setup_websocket()
endif()

if(WIN32 AND NOT MSVC)
    # Recent MinGW runtimes already link an asInvoker manifest. Probe the
    # actual compiler output so older toolchains still get our explicit one.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/manifest-probe.cpp" "int main() { return 0; }\n")
    try_compile(_tetr_manifest_probe_compiles
        "${CMAKE_CURRENT_BINARY_DIR}/manifest-probe-build"
        "${CMAKE_CURRENT_BINARY_DIR}/manifest-probe.cpp"
        COPY_FILE "${CMAKE_CURRENT_BINARY_DIR}/manifest-probe.exe")
    set(TETR_TOOLCHAIN_PROVIDES_MANIFEST OFF)
    if(_tetr_manifest_probe_compiles)
        file(STRINGS "${CMAKE_CURRENT_BINARY_DIR}/manifest-probe.exe" _tetr_manifest_lines
            REGEX "requestedExecutionLevel.*asInvoker" LIMIT_COUNT 1)
        if(_tetr_manifest_lines)
            set(TETR_TOOLCHAIN_PROVIDES_MANIFEST ON)
            message(STATUS "Compiler embeds an asInvoker manifest; skipping duplicate resource")
        endif()
    endif()
endif()
if(WIN32 AND NOT MSVC AND NOT TETR_TOOLCHAIN_PROVIDES_MANIFEST)
    enable_language(RC)
    set(TETR_WINDOWS_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows.manifest")
    configure_file(cmake/windows.rc.in windows_application.rc @ONLY)
    # windres may invoke its default preprocessor without quoting an install
    # path containing spaces (e.g. Program Files). Resolve it through PATH.
    get_filename_component(_tetr_rc_preprocessor "${CMAKE_CXX_COMPILER}" NAME)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/windows_application.rc"
        PROPERTIES COMPILE_OPTIONS
        "--preprocessor=${_tetr_rc_preprocessor};--preprocessor-arg=-E;--preprocessor-arg=-xc")
endif()

add_library(tetr_headers INTERFACE)
target_include_directories(tetr_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)


add_library(tetr_util STATIC
    src/util/worker_thread.cpp
    src/util/runtime_directory_lock.cpp
    src/util/numeric.cpp
    src/util/text_codec.cpp
    src/util/terminal_text.cpp
    src/util/deterministic_random.cpp
)
target_link_libraries(tetr_util PUBLIC tetr_headers Threads::Threads)

add_library(tetr_runtime STATIC
    src/runtime/piece_catalog_data.cpp
    src/runtime/piece_rotation_data.cpp
    src/runtime/playfield.cpp
    src/runtime/piece.cpp
    src/runtime/map_definition.cpp
    src/runtime/monotonic_clock.cpp
    src/runtime/runtime_services.cpp
    src/runtime/dynamic_rules.cpp
)
target_link_libraries(tetr_runtime PUBLIC tetr_headers tetr_util Threads::Threads)

add_library(tetr_replay_archive STATIC src/replay/multiplayer_codec.cpp)
target_link_libraries(tetr_replay_archive PUBLIC tetr_headers)

add_library(tetr_rules STATIC
    src/rules/lock_analysis.cpp
    src/rules/spin.cpp
    src/rules/chain.cpp
    src/rules/attack.cpp
    src/rules/scoring.cpp
    src/rules/spike.cpp
    src/rules/placement_resolution.cpp
    src/rules/qp_post_lock.cpp
    src/rules/quick_play.cpp
    src/rules/line_clear.cpp
    src/rules/garbage_queue.cpp
    src/rules/garbage_generation.cpp
    src/rules/frame_update.cpp
    src/rules/blitz.cpp
    src/rules/game_completion.cpp
)
target_link_libraries(tetr_rules PUBLIC tetr_headers PRIVATE tetr_util tetr_runtime)


add_library(tetr_presentation STATIC
    src/presentation/ui_surface.cpp
    src/presentation/virtual_controls.cpp
    src/presentation/text_field.cpp
    src/presentation/visibility.cpp
    src/presentation/board_view.cpp
)
target_link_libraries(tetr_presentation PUBLIC tetr_headers tetr_runtime)

# These tests build without terminal input, console output, Replay or a GPU.
include(CTest)
include(cmake/TestSuites.cmake)
if(BUILD_TESTING)
    enable_testing()
    tetr_test_module(frontend_snapshot_test tetr_core_tests tests/frontend_snapshot_test.cpp)
    target_link_libraries(frontend_snapshot_test PRIVATE tetr_presentation)
    add_test(NAME frontend_snapshot COMMAND tetr_core_tests frontend_snapshot_test)
    set_tests_properties(frontend_snapshot PROPERTIES TIMEOUT 10)
    tetr_test_module(frontend_ui_test tetr_core_tests tests/frontend_ui_test.cpp)
    target_link_libraries(frontend_ui_test PRIVATE tetr_presentation Threads::Threads)
    add_test(NAME frontend_ui COMMAND tetr_core_tests frontend_ui_test)
    set_tests_properties(frontend_ui PROPERTIES TIMEOUT 10)
    tetr_test_module(runtime_directory_lock_test tetr_core_tests tests/runtime_directory_lock_test.cpp)
    target_link_libraries(runtime_directory_lock_test PRIVATE tetr_util Threads::Threads)
    add_test(NAME runtime_directory_lock COMMAND tetr_core_tests runtime_directory_lock_test)
    set_tests_properties(runtime_directory_lock PROPERTIES TIMEOUT 15)
endif()

if(TETR_BUILD_TERMINAL OR TETR_BUILD_OPENGL OR TETR_BUILD_SERVER)
    add_library(tetr_network STATIC
        src/network/curl_runtime.cpp
        src/network/udp_socket.cpp
        src/network/websocket_socket.cpp
        src/network/network_services.cpp
        src/network/transport.cpp
        src/network/board_state_codec.cpp
        src/network/lobby_protocol.cpp
        src/network/hybrid_protocol.cpp
        src/network/board_view.cpp
        src/network/hybrid_sync.cpp
        src/network/lobby_channel.cpp
        src/network/frame_packet.cpp
        src/network/frame_pump.cpp
        src/network/multiplayer_channel.cpp
        src/network/relay_server.cpp
        src/network/multiplayer_relay.cpp
        src/network/multiplayer_client.cpp
        src/network/round_statistics.cpp
        src/network/room_config_protocol.cpp
        src/network/room_config_channel.cpp
        src/network/room_replay.cpp
        src/network/room_replay_server.cpp
    )
    target_link_libraries(tetr_network PUBLIC tetr_headers tetr_runtime tetr_replay_archive PRIVATE tetr_util tetr_rules)
    target_link_libraries(tetr_network PRIVATE CURL::libcurl)
    if(TETR_STATIC_DNS_ENABLED)
        target_sources(tetr_network PRIVATE src/network/portable_dns.cpp)
        target_compile_definitions(tetr_network PRIVATE TETR_PORTABLE_DNS=1)
        target_link_libraries(tetr_network PRIVATE c-ares::cares_static)
    endif()
    if(WIN32)
        target_link_libraries(tetr_network PUBLIC ws2_32)
    endif()
endif()

if(TETR_BUILD_SERVER)
    add_executable(tetr_server src/server.cpp)
    target_link_libraries(tetr_server PRIVATE tetr_headers tetr_network)
    if(TETR_PORTABLE_BUILD)
        install(TARGETS tetr_server RUNTIME DESTINATION . COMPONENT Portable)
    endif()
endif()

# A headless/server build never configures terminal input or BOT libraries.
if(NOT TETR_BUILD_TERMINAL AND NOT TETR_BUILD_OPENGL)
    if(BUILD_TESTING)
        foreach(name core_geometry core_rules)
            if(name STREQUAL "core_geometry")
                set(_suite tetr_core_tests)
            else()
                set(_suite tetr_rules_tests)
            endif()
            tetr_test_module(${name}_test ${_suite} tests/${name}_test.cpp)
            target_link_libraries(${name}_test PRIVATE tetr_rules tetr_runtime)
            add_test(NAME ${name} COMMAND ${_suite} ${name}_test)
        endforeach()
    endif()
    foreach(frontend_target tetr_terminal tetr_input tetr_render tetr_app tetr_bot_legacy)
        if(TARGET ${frontend_target})
            message(FATAL_ERROR "Headless/server build leaked a frontend target: ${frontend_target}")
        endif()
    endforeach()
    tetr_finalize_test_suites()
    return()
endif()

add_library(tetr_terminal STATIC
    src/render/terminal_viewport.cpp
    src/render/terminal_renderer.cpp
)
target_link_libraries(tetr_terminal PUBLIC tetr_headers PRIVATE tetr_util tetr_presentation)

add_library(tetr_config STATIC
    src/config/settings_schema.cpp
    src/config/user_config_file.cpp
    src/config/config_store.cpp
    src/config/virtual_controls.cpp
    src/config/keybinding_config.cpp
    src/config/presets.cpp
    src/config/normalization.cpp
    src/config/map_store.cpp
    src/config/multiplayer_config.cpp
    src/config/challenges.cpp
    src/config/challenge_prefix.cpp
    src/config/challenge_config.cpp
    src/config/challenge_catalog.cpp
    src/config/challenge_catalog_extended.cpp
)
target_link_libraries(tetr_config PUBLIC tetr_headers tetr_runtime tetr_util tetr_presentation)
if(BUILD_TESTING)
    tetr_test_module(virtual_controls_test tetr_core_tests tests/virtual_controls_test.cpp)
    target_link_libraries(virtual_controls_test PRIVATE tetr_config)
    add_test(NAME virtual_controls COMMAND tetr_core_tests virtual_controls_test)
    set_tests_properties(virtual_controls PROPERTIES TIMEOUT 15)
    tetr_test_module(shared_storage_test tetr_core_tests tests/shared_storage_test.cpp)
    target_link_libraries(shared_storage_test PRIVATE tetr_config Threads::Threads)
    add_test(NAME shared_storage COMMAND tetr_core_tests shared_storage_test)
    set_tests_properties(shared_storage PROPERTIES TIMEOUT 30)
endif()

add_library(tetr_input STATIC
    src/input/services.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tetr_input PRIVATE src/input/keyboard_monitor.cpp src/input/terminal_input.cpp)
endif()
target_link_libraries(tetr_input PUBLIC tetr_headers Threads::Threads)
target_link_libraries(tetr_terminal PRIVATE tetr_input)

add_library(tetr_presentation_context STATIC src/presentation/context_view.cpp)
target_link_libraries(tetr_presentation_context PUBLIC tetr_presentation tetr_runtime PRIVATE tetr_replay tetr_rules tetr_network)

add_library(tetr_bot_zzztoj_upstream STATIC
    zzztoj_standalone/upstream/src/ai_zzz.cpp
    zzztoj_standalone/upstream/src/integer_utils.cpp
    zzztoj_standalone/upstream/src/random.cpp
    zzztoj_standalone/upstream/src/rule_botris.cpp
    zzztoj_standalone/upstream/src/rule_srs.cpp
    zzztoj_standalone/upstream/src/search_aspin.cpp
    zzztoj_standalone/upstream/src/search_cautious.cpp
    zzztoj_standalone/upstream/src/search_path.cpp
    zzztoj_standalone/upstream/src/search_simple.cpp
    zzztoj_standalone/upstream/src/search_simulate.cpp
    zzztoj_standalone/upstream/src/search_tspin.cpp
    zzztoj_standalone/upstream/src/tetris_core.cpp
)
target_include_directories(tetr_bot_zzztoj_upstream
    SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/zzztoj_standalone/upstream/src)
target_link_libraries(tetr_bot_zzztoj_upstream PUBLIC Threads::Threads)

add_library(tetr_bot_zzztoj STATIC
    src/bot/bot_zzztoj.cpp
    zzztoj_standalone/src/bot_adapter.cpp
    zzztoj_standalone/src/rule_tetr_srs_plus.cpp
)
target_include_directories(tetr_bot_zzztoj
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/zzztoj_standalone/src)
target_link_libraries(tetr_bot_zzztoj
    PRIVATE tetr_headers tetr_runtime tetr_rules tetr_bot_zzztoj_upstream Threads::Threads)

add_library(tetr_bot_chatgpt STATIC src/bot/bot_chatgpt.cpp)
option(TETR_CHATGPT_P2_A "Enable experimental bot_chatgpt Spin-exit search quota" OFF)
option(TETR_CHATGPT_P2_B "Enable experimental bot_chatgpt multi-branch Spin quota" OFF)
option(TETR_CHATGPT_P3_A "Enable experimental bot_chatgpt empty-HOLD preview correction" OFF)
option(TETR_CHATGPT_P3_B "Enable experimental bot_chatgpt post-rise reachability guard" OFF)
option(TETR_CHATGPT_P5_A "Enable experimental bot_chatgpt full-entry movement coverage" OFF)
option(TETR_CHATGPT_TERRAIN_V1 "Enable experimental bot_chatgpt recoverable-terrain evaluation" OFF)
option(TETR_CHATGPT_TERRAIN_V2 "Enable experimental bot_chatgpt removable-bridge evaluation" OFF)
option(TETR_CHATGPT_LEARNED_TERRAIN "Enable experimental bot_chatgpt frozen Moch terrain correction" OFF)
option(TETR_CHATGPT_LOCAL_PATTERN "Enable experimental bot_chatgpt frozen Moch local-pattern correction" OFF)
option(TETR_CHATGPT_TIME_B2B "Enable experimental bot_chatgpt terminal B2B time discount" OFF)
option(TETR_CHATGPT_KNOWN_BAG "Enable experimental bot_chatgpt singleton bag-complement preview" OFF)
option(TETR_CHATGPT_O_ROOF "Enable experimental bot_chatgpt O-only skyline descent correction" OFF)
option(TETR_CHATGPT_MOCH_MULTI_STEP "Enable experimental bot_chatgpt frozen five-step Moch terrain scale" OFF)
option(TETR_CHATGPT_RISE_HORIZON "Stop experimental bot_chatgpt valuation after unknown garbage rises" OFF)
option(TETR_CHATGPT_TRANSPOSITION "Skip dominated equivalent states in experimental bot_chatgpt search" OFF)
option(TETR_CHATGPT_GARBAGE_ROLLOUT "Evaluate visible garbage using hypothetical hole rollouts in experimental bot_chatgpt" OFF)
option(TETR_CHATGPT_ROOT_COVERAGE "Complete current-root movement coverage in experimental bot_chatgpt" OFF)
option(TETR_CHATGPT_GARBAGE_BEAM "Keep two continuation states in experimental visible-garbage rollouts" OFF)
option(TETR_CHATGPT_SETTLED_RISK "Accumulate path risk from physical height while retaining terminal incoming reserve" OFF)
option(TETR_CHATGPT_FAST_TERRAIN "Compute original bot_chatgpt terrain exactly with sparse hole scans" OFF)
option(TETR_CHATGPT_CLEAR_STALL_RISK "Refund buffered path risk only when a clear stalls garbage" OFF)
option(TETR_CHATGPT_CLEAR_BURST "Charge peak physical risk once per consecutive clearing burst" OFF)
option(TETR_CHATGPT_CLEAR_RECOVERY "Order temporary covers using legal visible one/two-clear recovery witnesses" OFF)
option(TETR_CHATGPT_SETUP_RECOVERY "Allow one setup before a proved clear when known incoming is empty" OFF)
option(TETR_CHATGPT_CHARGE_RESERVE "Value stored releasable B2B charge at the attack unit weight" OFF)
option(TETR_CHATGPT_CHARGE_BURST "Combine stored charge valuation with clearing burst risk" OFF)
option(TETR_CHATGPT_SPAWN_COLUMNS "Use normal spawn columns for the experimental pressure height" OFF)
option(TETR_CHATGPT_FLAT_ATTACK "Remove depth discount only from real attack reward" OFF)
option(TETR_CHATGPT_BURST_WINDOW "Value bounded net outgoing pressure concentrated in three placements" OFF)
option(TETR_CHATGPT_VISIBLE_PATH_RISK "Refund intermediate exposure only with known geometry and a checked visible next spawn" OFF)
option(TETR_CHATGPT_RECOVERED_PATH_RISK "Refund known exposure after a checked continuation returns to zero baseline danger" OFF)
option(TETR_CHATGPT_RELEASE_CHOICE "Retain reachable ordinary T clears alongside Spin variants for B2B release choices" OFF)
option(TETR_CHATGPT_BAG_TAIL "Evaluate visible frontier plans over possible public-bag continuations" OFF)
option(TETR_CHATGPT_MATERIAL_RESERVE "Value a bounded working stack as material for future attacks" OFF)
option(TETR_CHATGPT_OWN_MATERIAL "Value own working material while excluding nine original cells per garbage row" OFF)
option(TETR_CHATGPT_RELATIVE_COVER "Price covered-cell repair by local depth instead of absolute elevation" OFF)
option(TETR_CHATGPT_KEY_QUAD "Project a reachable reserved I quad and one-step preparation during search" OFF)
option(TETR_CHATGPT_KEY_PIECE "Project reserved key clears and one-piece completions with persistent geometry caching" OFF)
if(TETR_CHATGPT_SETUP_RECOVERY OR TETR_CHATGPT_CHARGE_RESERVE OR TETR_CHATGPT_CHARGE_BURST OR TETR_CHATGPT_SPAWN_COLUMNS OR TETR_CHATGPT_FLAT_ATTACK OR TETR_CHATGPT_BURST_WINDOW OR TETR_CHATGPT_VISIBLE_PATH_RISK OR TETR_CHATGPT_RECOVERED_PATH_RISK OR TETR_CHATGPT_RELEASE_CHOICE OR TETR_CHATGPT_BAG_TAIL OR TETR_CHATGPT_MATERIAL_RESERVE OR TETR_CHATGPT_RELATIVE_COVER OR TETR_CHATGPT_OWN_MATERIAL OR TETR_CHATGPT_KEY_QUAD OR TETR_CHATGPT_KEY_PIECE)
    get_cmake_property(_chatgpt_cache_variables CACHE_VARIABLES)
    set(_chatgpt_enabled_count 0)
    foreach(_chatgpt_option IN LISTS _chatgpt_cache_variables)
        if(_chatgpt_option MATCHES "^TETR_CHATGPT_")
            if(${_chatgpt_option})
                math(EXPR _chatgpt_enabled_count "${_chatgpt_enabled_count} + 1")
            endif()
        endif()
    endforeach()
    if(_chatgpt_enabled_count GREATER 1)
        message(FATAL_ERROR "Enable exactly one frozen bot_chatgpt experiment")
    endif()
endif()
foreach(_chatgpt_new_option TETR_CHATGPT_SETUP_RECOVERY TETR_CHATGPT_CHARGE_RESERVE TETR_CHATGPT_CHARGE_BURST TETR_CHATGPT_SPAWN_COLUMNS TETR_CHATGPT_FLAT_ATTACK TETR_CHATGPT_BURST_WINDOW TETR_CHATGPT_VISIBLE_PATH_RISK TETR_CHATGPT_RECOVERED_PATH_RISK TETR_CHATGPT_RELEASE_CHOICE TETR_CHATGPT_BAG_TAIL TETR_CHATGPT_MATERIAL_RESERVE TETR_CHATGPT_RELATIVE_COVER TETR_CHATGPT_OWN_MATERIAL TETR_CHATGPT_KEY_QUAD TETR_CHATGPT_KEY_PIECE)
    if(${_chatgpt_new_option})
        target_compile_definitions(tetr_bot_chatgpt PRIVATE ${_chatgpt_new_option}=1)
    endif()
endforeach()
if(TETR_CHATGPT_CLEAR_RECOVERY AND (TETR_CHATGPT_CLEAR_BURST OR TETR_CHATGPT_CLEAR_STALL_RISK OR TETR_CHATGPT_FAST_TERRAIN OR TETR_CHATGPT_SETTLED_RISK OR TETR_CHATGPT_GARBAGE_BEAM OR TETR_CHATGPT_ROOT_COVERAGE OR TETR_CHATGPT_GARBAGE_ROLLOUT OR TETR_CHATGPT_TRANSPOSITION OR TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt clear recovery candidate separately from other experiments")
endif()
if(TETR_CHATGPT_CLEAR_RECOVERY)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_CLEAR_RECOVERY=1)
endif()
if(TETR_CHATGPT_CLEAR_BURST AND (TETR_CHATGPT_CLEAR_STALL_RISK OR TETR_CHATGPT_FAST_TERRAIN OR TETR_CHATGPT_SETTLED_RISK OR TETR_CHATGPT_GARBAGE_BEAM OR TETR_CHATGPT_ROOT_COVERAGE OR TETR_CHATGPT_GARBAGE_ROLLOUT OR TETR_CHATGPT_TRANSPOSITION OR TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt clearing burst candidate separately from other experiments")
endif()
if(TETR_CHATGPT_CLEAR_BURST)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_CLEAR_BURST=1)
endif()
if(TETR_CHATGPT_CLEAR_STALL_RISK AND (TETR_CHATGPT_FAST_TERRAIN OR TETR_CHATGPT_SETTLED_RISK OR TETR_CHATGPT_GARBAGE_BEAM OR TETR_CHATGPT_ROOT_COVERAGE OR TETR_CHATGPT_GARBAGE_ROLLOUT OR TETR_CHATGPT_TRANSPOSITION OR TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt clear-stall risk candidate separately from other experiments")
endif()
if(TETR_CHATGPT_CLEAR_STALL_RISK)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_CLEAR_STALL_RISK=1)
endif()
if(TETR_CHATGPT_FAST_TERRAIN AND (TETR_CHATGPT_SETTLED_RISK OR TETR_CHATGPT_GARBAGE_BEAM OR TETR_CHATGPT_ROOT_COVERAGE OR TETR_CHATGPT_GARBAGE_ROLLOUT OR TETR_CHATGPT_TRANSPOSITION OR TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt exact fast terrain candidate separately from other experiments")
endif()
if(TETR_CHATGPT_FAST_TERRAIN)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_FAST_TERRAIN=1)
endif()
if(TETR_CHATGPT_SETTLED_RISK AND (TETR_CHATGPT_GARBAGE_BEAM OR TETR_CHATGPT_ROOT_COVERAGE OR TETR_CHATGPT_GARBAGE_ROLLOUT OR TETR_CHATGPT_TRANSPOSITION OR TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt settled risk candidate separately from other experiments")
endif()
if(TETR_CHATGPT_SETTLED_RISK)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_SETTLED_RISK=1)
endif()
if(TETR_CHATGPT_GARBAGE_BEAM AND (TETR_CHATGPT_ROOT_COVERAGE OR TETR_CHATGPT_GARBAGE_ROLLOUT OR TETR_CHATGPT_TRANSPOSITION OR TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt two-state garbage candidate separately from other experiments")
endif()
if(TETR_CHATGPT_GARBAGE_BEAM)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_GARBAGE_BEAM=1)
endif()
if(TETR_CHATGPT_ROOT_COVERAGE AND (TETR_CHATGPT_GARBAGE_ROLLOUT OR TETR_CHATGPT_TRANSPOSITION OR TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt root coverage candidate separately from other experiments")
endif()
if(TETR_CHATGPT_ROOT_COVERAGE)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_ROOT_COVERAGE=1)
endif()
if(TETR_CHATGPT_GARBAGE_ROLLOUT AND (TETR_CHATGPT_TRANSPOSITION OR TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt garbage rollout candidate separately from other experiments")
endif()
if(TETR_CHATGPT_GARBAGE_ROLLOUT)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_GARBAGE_ROLLOUT=1)
endif()
if(TETR_CHATGPT_TRANSPOSITION AND (TETR_CHATGPT_RISE_HORIZON OR TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt transposition candidate separately from other experiments")
endif()
if(TETR_CHATGPT_TRANSPOSITION)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_TRANSPOSITION=1)
endif()
if(TETR_CHATGPT_RISE_HORIZON AND (TETR_CHATGPT_MOCH_MULTI_STEP OR TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt rise horizon separately from other experiments")
endif()
if(TETR_CHATGPT_RISE_HORIZON)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_RISE_HORIZON=1)
endif()
if(TETR_CHATGPT_MOCH_MULTI_STEP AND (TETR_CHATGPT_O_ROOF OR TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the frozen bot_chatgpt five-step Moch terrain candidate separately from other experiments")
endif()
if(TETR_CHATGPT_MOCH_MULTI_STEP)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_MOCH_MULTI_STEP=1)
endif()
if(TETR_CHATGPT_O_ROOF AND (TETR_CHATGPT_KNOWN_BAG OR TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt O-only skyline candidate separately from other experiments")
endif()
if(TETR_CHATGPT_O_ROOF)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_O_ROOF=1)
endif()
if(TETR_CHATGPT_KNOWN_BAG AND (TETR_CHATGPT_TIME_B2B OR TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt singleton bag-complement candidate separately from other experiments")
endif()
if(TETR_CHATGPT_KNOWN_BAG)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_KNOWN_BAG=1)
endif()
if(TETR_CHATGPT_TIME_B2B AND (TETR_CHATGPT_LOCAL_PATTERN OR TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt terminal B2B time discount separately from other experiments")
endif()
if(TETR_CHATGPT_TIME_B2B)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_TIME_B2B=1)
endif()
if(TETR_CHATGPT_LOCAL_PATTERN AND (TETR_CHATGPT_LEARNED_TERRAIN OR TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the frozen bot_chatgpt local-pattern correction separately from other experiments")
endif()
if(TETR_CHATGPT_LOCAL_PATTERN)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_LOCAL_PATTERN=1)
endif()
if(TETR_CHATGPT_LEARNED_TERRAIN AND (TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2 OR TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the frozen bot_chatgpt learned correction separately from other experiments")
endif()
if(TETR_CHATGPT_LEARNED_TERRAIN)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_LEARNED_TERRAIN=1)
endif()
if(TETR_CHATGPT_TERRAIN_V1 AND TETR_CHATGPT_TERRAIN_V2)
    message(FATAL_ERROR "Choose only one bot_chatgpt terrain experiment")
endif()
if((TETR_CHATGPT_TERRAIN_V1 OR TETR_CHATGPT_TERRAIN_V2) AND (TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B OR TETR_CHATGPT_P5_A))
    message(FATAL_ERROR "Test the bot_chatgpt terrain model separately from earlier experiments")
endif()
if(TETR_CHATGPT_TERRAIN_V2)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_TERRAIN_V2=1)
endif()
if(TETR_CHATGPT_TERRAIN_V1)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_TERRAIN_V1=1)
endif()
if(TETR_CHATGPT_P5_A AND (TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A OR TETR_CHATGPT_P3_B))
    message(FATAL_ERROR "Keep bot_chatgpt P5-A separate from earlier experiments")
endif()
if(TETR_CHATGPT_P5_A)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_P5_A=1)
endif()
if(TETR_CHATGPT_P3_B AND (TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B OR TETR_CHATGPT_P3_A))
    message(FATAL_ERROR "Keep bot_chatgpt P3-B separate from all other experiments")
endif()
if(TETR_CHATGPT_P3_A AND (TETR_CHATGPT_P2_A OR TETR_CHATGPT_P2_B))
    message(FATAL_ERROR "Keep bot_chatgpt P3 model experiments separate from P2 search experiments")
endif()
if(TETR_CHATGPT_P2_A AND TETR_CHATGPT_P2_B)
    message(FATAL_ERROR "Choose only one bot_chatgpt P2 experiment")
endif()
if(TETR_CHATGPT_P2_A)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_P2_A=1)
endif()
if(TETR_CHATGPT_P2_B)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_P2_B=1)
endif()
if(TETR_CHATGPT_P3_A)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_P3_A=1)
endif()
if(TETR_CHATGPT_P3_B)
    target_compile_definitions(tetr_bot_chatgpt PRIVATE TETR_CHATGPT_P3_B=1)
endif()
target_include_directories(tetr_bot_chatgpt PRIVATE zzztoj_standalone/src)
target_link_libraries(tetr_bot_chatgpt
    PRIVATE tetr_headers tetr_runtime tetr_rules tetr_engine_core tetr_bot_zzztoj tetr_bot_zzztoj_upstream)

add_library(tetr_bot_quick_play STATIC src/bot/quick_play_search.cpp)
target_link_libraries(tetr_bot_quick_play PUBLIC tetr_headers tetr_runtime tetr_rules tetr_engine_core)
target_link_libraries(tetr_bot_zzztoj PRIVATE tetr_bot_quick_play)
target_link_libraries(tetr_bot_chatgpt PRIVATE tetr_bot_quick_play)
add_library(tetr_bot_capabilities STATIC src/bot/availability.cpp)
target_link_libraries(tetr_bot_capabilities PUBLIC tetr_headers tetr_runtime)
target_link_libraries(tetr_bot_capabilities PRIVATE tetr_bot_quick_play)
target_link_libraries(tetr_bot_zzztoj PRIVATE tetr_bot_capabilities)
target_link_libraries(tetr_bot_chatgpt PRIVATE tetr_bot_capabilities)

add_library(tetr_bot_coldclear STATIC
    src/bot/bot_coldclear.cpp
    src/bot/coldclear/board.cpp
    src/bot/coldclear/evaluation.cpp
    src/bot/coldclear/search.cpp
)
target_link_libraries(tetr_bot_coldclear
    PRIVATE tetr_headers tetr_runtime tetr_rules tetr_engine_core tetr_bot_capabilities)

add_library(tetr_bot_legacy STATIC
    src/legacy/bot.cpp
    src/legacy/bot_strategy.cpp
    src/bot/bot_while1.cpp
    src/bot/bot_candy0014.cpp
    src/bot/bot_mech.cpp
)
target_link_libraries(tetr_bot_legacy
    PUBLIC tetr_engine_core
    PRIVATE tetr_headers tetr_runtime tetr_util tetr_rules tetr_bot_zzztoj tetr_bot_chatgpt tetr_bot_coldclear tetr_bot_capabilities)


add_library(tetr_render STATIC
    src/render/audio_cues.cpp
    src/render/board_layout_renderer.cpp
    src/render/board_stats_renderer.cpp
    src/render/board_event_renderer.cpp
    src/render/piece_renderer.cpp
    src/render/spawn_marker.cpp
)
target_link_libraries(tetr_render PUBLIC tetr_headers tetr_runtime tetr_terminal tetr_presentation_context tetr_replay tetr_rules ${CMAKE_DL_LIBS})

add_library(tetr_replay STATIC
    src/replay/challenge_prefix.cpp
    src/replay/file_path.cpp
    src/replay/recent.cpp
    src/replay/playback_checkpoint.cpp
    src/replay/playback_clock.cpp
    src/replay/turn_timeline.cpp
    src/replay/operation_codec.cpp
    src/replay/config_codec.cpp
    src/replay/local_versus_codec.cpp
    src/replay/movement_event.cpp
    src/replay/line_clear_event.cpp
    src/replay/replay_hash.cpp
    src/replay/recorder.cpp
    src/replay/tetrio_inputs.cpp
    src/replay/tetrio_recorder.cpp
)
target_link_libraries(tetr_replay PUBLIC tetr_headers tetr_runtime tetr_util tetr_replay_archive PRIVATE tetr_rules)

add_library(tetr_engine_core STATIC
    src/engine/movement_input_state.cpp
    src/engine/field_mutation.cpp
    src/engine/garbage_field.cpp
    src/engine/board_lifecycle.cpp
    src/engine/piece_queue.cpp
    src/engine/placement_application.cpp
    src/engine/movement_controller.cpp
    src/engine/movement_presentation.cpp
    src/engine/autoplay_path.cpp
    src/engine/manual_input.cpp
    src/engine/periodic_events.cpp
    src/engine/frame_board_adapter.cpp
    src/engine/placement_board_adapter.cpp
    src/engine/locked_piece_resolution.cpp
)
target_link_libraries(tetr_engine_core
    PUBLIC tetr_headers tetr_rules tetr_runtime
    PRIVATE tetr_util)

# Presentation state machines and terminal-facing helpers are kept out of the
# core engine.  They may render, but they do not own Network/Replay transport.
add_library(tetr_engine_presentation STATIC
    src/engine/input_precharge.cpp
    src/engine/line_clear_presentation.cpp
    src/engine/garbage_rendering.cpp
    src/engine/next_preview_presentation.cpp
    src/engine/placement_presentation.cpp
    src/engine/countdown_presentation.cpp
    src/engine/multiplayer_layout.cpp
    src/engine/game_end_presentation.cpp
)
target_link_libraries(tetr_engine_presentation
    PUBLIC tetr_headers tetr_engine_core tetr_rules tetr_runtime tetr_render tetr_input)

# Explicit adapters for Replay/Network and other process-boundary side effects.
# Gameplay can depend on this layer; core and presentation must not.
add_library(tetr_engine_integration STATIC
    src/engine/undo.cpp
    src/engine/pause.cpp
    src/engine/garbage_generation_adapter.cpp
    src/engine/garbage_buffer_adapter.cpp
    src/engine/autoplay_adapter.cpp
    src/engine/autoplay_handling.cpp
    src/engine/piece_replay.cpp
    src/engine/frame_coordinator.cpp
    src/engine/multiplayer_scene.cpp
    src/engine/placement_side_effects.cpp
    src/engine/replay_application.cpp
    src/engine/replay_playback_controls.cpp
    src/engine/remote_board_state_adapter.cpp
    src/engine/placement_replay_sync.cpp
    src/engine/spike_presentation.cpp
    src/engine/game_terminal_layout.cpp
)
target_link_libraries(tetr_engine_integration
    PUBLIC tetr_headers tetr_engine_core tetr_engine_presentation
           tetr_rules tetr_runtime tetr_render tetr_network tetr_replay tetr_input
    PRIVATE tetr_util)

# High-level per-piece / per-game orchestration.  This is the composition layer
# for Engine facilities rather than the owner of the low-level algorithms.
add_library(tetr_gameplay STATIC
    src/engine/board_qp_init.cpp
    src/engine/player_input.cpp
    src/engine/live_piece_controller.cpp
    src/engine/live_piece_controls.cpp
    src/engine/live_piece_periodic_events.cpp
    src/engine/piece_spawn.cpp
    src/engine/entry_delay.cpp
    src/engine/placement_coordinator.cpp
    src/engine/finesse.cpp
    src/engine/game_loop_adapter.cpp
    src/engine/locked_line_clear_phase.cpp
    src/engine/simulation_time.cpp
)
target_link_libraries(tetr_gameplay
    PUBLIC tetr_headers tetr_engine_core tetr_engine_presentation tetr_engine_integration
           tetr_rules tetr_runtime tetr_render tetr_network tetr_replay tetr_input)

# Compatibility umbrella for existing callers.  It intentionally contains no
# sources: consumers can migrate to the narrow targets above incrementally.
add_library(tetr_engine INTERFACE)
target_link_libraries(tetr_engine INTERFACE
    tetr_engine_core tetr_engine_presentation tetr_engine_integration tetr_gameplay)

add_library(tetr_app STATIC
    src/app/client_loop.cpp
    src/app/init.cpp
    src/app/init_replay.cpp
    src/app/init_multiplayer.cpp
    src/app/multiplayer_battle.cpp
    src/app/init_player_setup.cpp
    src/app/game_terminal.cpp
    src/app/place.cpp
    src/app/game.cpp
    src/app/local_versus.cpp
    src/app/settings_menu.cpp
    src/app/settings_model.cpp
    src/app/settings_targets.cpp
    src/app/settings_presets.cpp
    src/app/settings_preset_editor.cpp
    src/app/settings_maps.cpp
    src/app/map_editor.cpp
    src/app/settings_view.cpp
    src/app/settings_navigation.cpp
    src/app/settings_input.cpp
    src/app/ui_text.cpp
    src/app/start_menu.cpp
    src/app/start_screen_navigation.cpp
    src/app/replay_playback.cpp
    src/app/online_replay.cpp
    src/app/room_replay_playback.cpp
    src/app/post_game_interaction.cpp
    src/app/post_game_statistics.cpp
    src/app/single_player_challenges.cpp
    src/app/challenge_replay_analysis.cpp
    src/app/bot_battle_royale.cpp
)
target_link_libraries(tetr_app PUBLIC tetr_headers tetr_runtime tetr_rules tetr_engine tetr_replay tetr_input tetr_config tetr_bot_capabilities)

if(TETR_BUILD_TERMINAL)
add_executable(tetr src/tetr.cpp)
target_link_libraries(tetr PRIVATE tetr_headers tetr_app tetr_bot_legacy Threads::Threads)

add_executable(tetr_ft7 src/tetr_ft7.cpp)
target_link_libraries(tetr_ft7 PRIVATE tetr_headers tetr_app tetr_bot_legacy Threads::Threads)
endif()

if(TETR_BUILD_OPENGL)
    include(cmake/OpenGLFrontend.cmake)
endif()
if(NOT ANDROID)
    include(cmake/ClientDistribution.cmake)
endif()

if(TETR_PORTABLE_BUILD)
    if(TETR_BUILD_TERMINAL)
        install(TARGETS tetr RUNTIME DESTINATION . COMPONENT Portable)
    endif()
    configure_file(cmake/portable/InstallResources.cmake.in
        portable-install-resources.cmake @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/portable-install-resources.cmake"
        COMPONENT Portable)
endif()


if(BUILD_TESTING AND TETR_BUILD_TERMINAL)
    # A bundled dependency may have included CTest with its tests disabled.
    # Explicitly enable the parent project's tests regardless of that guard.
    enable_testing()
    tetr_test_module(bit_ops_test tetr_core_tests tests/bit_ops_test.cpp)
    target_link_libraries(bit_ops_test PRIVATE tetr_headers)
    add_test(NAME bit_ops COMMAND tetr_core_tests bit_ops_test)

    tetr_test_module(opener_phase_test tetr_core_tests tests/opener_phase_test.cpp)
    target_link_libraries(opener_phase_test PRIVATE tetr_headers)
    add_test(NAME opener_phase COMMAND tetr_core_tests opener_phase_test)


    tetr_test_module(core_geometry_test tetr_core_tests tests/core_geometry_test.cpp)
    target_link_libraries(core_geometry_test PRIVATE tetr_runtime)
    add_test(NAME core_geometry COMMAND tetr_core_tests core_geometry_test)

    tetr_test_module(core_rules_test tetr_rules_tests tests/core_rules_test.cpp)
    target_link_libraries(core_rules_test PRIVATE tetr_rules)
    add_test(NAME core_rules COMMAND tetr_rules_tests core_rules_test)

    tetr_test_module(attack_rules_test tetr_application_tests tests/attack_rules_test.cpp)
    target_link_libraries(attack_rules_test PRIVATE tetr_app)
    add_test(NAME attack_rules COMMAND tetr_application_tests attack_rules_test)

    tetr_test_module(garbage_shape_test tetr_application_tests tests/garbage_shape_test.cpp)
    target_link_libraries(garbage_shape_test PRIVATE tetr_app)
    add_test(NAME garbage_shape COMMAND tetr_application_tests garbage_shape_test)

    tetr_test_module(game_terminal_test tetr_application_tests tests/game_terminal_test.cpp)
    target_link_libraries(game_terminal_test PRIVATE tetr_app)
    add_test(NAME game_terminal COMMAND tetr_application_tests game_terminal_test)
    set_tests_properties(game_terminal PROPERTIES TIMEOUT 10)

    tetr_test_module(spawn_marker_test tetr_application_tests tests/spawn_marker_test.cpp)
    target_link_libraries(spawn_marker_test PRIVATE tetr_app)
    add_test(NAME spawn_marker COMMAND tetr_application_tests spawn_marker_test)
    set_tests_properties(spawn_marker PROPERTIES TIMEOUT 10)

    tetr_test_module(spin_bonuses_test tetr_rules_tests tests/spin_bonuses_test.cpp)
    target_link_libraries(spin_bonuses_test PRIVATE tetr_rules tetr_config tetr_replay)
    add_test(NAME spin_bonuses COMMAND tetr_rules_tests spin_bonuses_test)

    tetr_test_module(rotation_systems_test tetr_rules_tests tests/rotation_systems_test.cpp)
    target_link_libraries(rotation_systems_test PRIVATE tetr_rules tetr_config tetr_replay tetr_engine)
    add_test(NAME rotation_systems COMMAND tetr_rules_tests rotation_systems_test)

    tetr_test_module(bag_randomizer_test tetr_rules_tests tests/bag_randomizer_test.cpp)
    target_link_libraries(bag_randomizer_test PRIVATE tetr_rules tetr_config tetr_replay tetr_engine)
    add_test(NAME bag_randomizer COMMAND tetr_rules_tests bag_randomizer_test)

    tetr_test_module(spike_test tetr_rules_tests tests/spike_test.cpp)
    target_link_libraries(spike_test PRIVATE tetr_rules tetr_runtime)
    add_test(NAME spike COMMAND tetr_rules_tests spike_test)

    tetr_test_module(post_lock_rules_test tetr_rules_tests tests/post_lock_rules_test.cpp)
    target_link_libraries(post_lock_rules_test PRIVATE tetr_rules tetr_engine)
    add_test(NAME post_lock_rules COMMAND tetr_rules_tests post_lock_rules_test)
    tetr_test_module(quick_play_rules_test tetr_rules_tests tests/quick_play_rules_test.cpp)
    target_link_libraries(quick_play_rules_test PRIVATE tetr_rules tetr_engine tetr_render tetr_replay tetr_config)
    add_test(NAME quick_play_rules COMMAND tetr_rules_tests quick_play_rules_test)
    tetr_test_module(snowball_test tetr_rules_tests tests/snowball_test.cpp)
    target_link_libraries(snowball_test PRIVATE tetr_config tetr_engine tetr_render tetr_replay)
    add_test(NAME snowball COMMAND tetr_rules_tests snowball_test)

    tetr_test_module(undo_test tetr_application_tests tests/undo_test.cpp)
    target_link_libraries(undo_test PRIVATE tetr_app)
    add_test(NAME undo COMMAND tetr_application_tests undo_test)
    tetr_test_module(pause_test tetr_application_tests tests/pause_test.cpp)
    target_link_libraries(pause_test PRIVATE tetr_app)
    add_test(NAME pause COMMAND tetr_application_tests pause_test)
    set_tests_properties(pause PROPERTIES TIMEOUT 10)
    set_tests_properties(undo PROPERTIES TIMEOUT 15)
    add_test(NAME quick_play_cli COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:tetr>
        -DWORK_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/quick_play_cli_fixture
        -DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/quick_play_cli_test.cmake)

    tetr_test_module(garbage_system_test tetr_rules_tests tests/garbage_system_test.cpp)
    target_link_libraries(garbage_system_test PRIVATE tetr_rules tetr_engine)
    add_test(NAME garbage_system COMMAND tetr_rules_tests garbage_system_test)

    tetr_test_module(placement_application_test tetr_rules_tests tests/placement_application_test.cpp)
    target_link_libraries(placement_application_test PRIVATE tetr_rules tetr_engine_core)
    add_test(NAME placement_application COMMAND tetr_rules_tests placement_application_test)

    tetr_test_module(movement_controller_test tetr_engine_tests tests/movement_controller_test.cpp)
    target_link_libraries(movement_controller_test PRIVATE tetr_engine)
    add_test(NAME movement_controller COMMAND tetr_engine_tests movement_controller_test)

    tetr_test_module(movement_presentation_test tetr_engine_tests tests/movement_presentation_test.cpp)
    target_link_libraries(movement_presentation_test PRIVATE tetr_engine_presentation tetr_replay)
    add_test(NAME movement_presentation COMMAND tetr_engine_tests movement_presentation_test)

    tetr_test_module(tetrio_inputs_test tetr_engine_tests tests/tetrio_inputs_test.cpp)
    target_link_libraries(tetrio_inputs_test PRIVATE tetr_replay tetr_engine_presentation)
    add_test(NAME tetrio_inputs COMMAND tetr_engine_tests tetrio_inputs_test)

    tetr_test_module(tetrio_random_test tetr_rules_tests tests/tetrio_random_test.cpp)
    target_link_libraries(tetrio_random_test PRIVATE tetr_headers)
    add_test(NAME tetrio_random COMMAND tetr_rules_tests tetrio_random_test)

    tetr_test_module(tetrio_round_test tetr_application_tests tests/tetrio_round_test.cpp)
    target_link_libraries(tetrio_round_test PRIVATE tetr_app)
    add_test(NAME tetrio_round COMMAND tetr_application_tests tetrio_round_test ${CMAKE_CURRENT_BINARY_DIR}/tetrio_round_fixture)
    set_tests_properties(tetrio_round PROPERTIES ENVIRONMENT
        "TETR_TEST_CLOCK_SCALE=100;TETR_TEST_MAX_PIECES=1;TETR_TEST_MAX_SECONDS=0.01;TETR_TEST_MAX_GARBAGE_CLEARED=1")
    add_test(NAME tetrio_ft7_cli COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:tetr_ft7>
        -DEXISTING_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/tetrio_ft7_cli_test.cmake)

    tetr_test_module(line_clear_presentation_test tetr_engine_tests tests/line_clear_presentation_test.cpp)
    target_link_libraries(line_clear_presentation_test PRIVATE tetr_engine_presentation tetr_replay)
    add_test(NAME line_clear_presentation COMMAND tetr_engine_tests line_clear_presentation_test)

    tetr_test_module(frame_update_test tetr_engine_tests tests/frame_update_test.cpp)
    target_link_libraries(frame_update_test PRIVATE tetr_rules tetr_engine)
    add_test(NAME frame_update COMMAND tetr_engine_tests frame_update_test)

    tetr_test_module(periodic_and_input_test tetr_engine_tests tests/periodic_and_input_test.cpp)
    target_link_libraries(periodic_and_input_test PRIVATE tetr_engine)
    add_test(NAME periodic_and_input COMMAND tetr_engine_tests periodic_and_input_test)

    tetr_test_module(player_input_equivalence_test tetr_engine_tests tests/player_input_equivalence_test.cpp)
    target_link_libraries(player_input_equivalence_test PRIVATE tetr_engine)
    add_test(NAME player_input_equivalence COMMAND tetr_engine_tests player_input_equivalence_test)

    tetr_test_module(gameplay_input_buffer_test tetr_engine_tests tests/gameplay_input_buffer_test.cpp)
    target_link_libraries(gameplay_input_buffer_test PRIVATE tetr_engine)
    add_test(NAME gameplay_input_buffer COMMAND tetr_engine_tests gameplay_input_buffer_test)

    tetr_test_module(handling_controls_test tetr_application_tests tests/handling_controls_test.cpp)
    target_link_libraries(handling_controls_test PRIVATE tetr_app)
    add_test(NAME handling_controls COMMAND tetr_application_tests handling_controls_test)
    tetr_test_module(forced_handling_test tetr_application_tests tests/forced_handling_test.cpp)
    target_link_libraries(forced_handling_test PRIVATE tetr_app)
    add_test(NAME forced_handling COMMAND tetr_application_tests forced_handling_test)
    set_tests_properties(handling_controls PROPERTIES TIMEOUT 10)

    tetr_test_module(das_precharge_test tetr_application_tests tests/das_precharge_test.cpp)
    target_link_libraries(das_precharge_test PRIVATE tetr_app)
    add_test(NAME das_precharge COMMAND tetr_application_tests das_precharge_test)
    set_tests_properties(das_precharge PROPERTIES TIMEOUT 15 ENVIRONMENT
        "TETR_TEST_CLOCK_SCALE=20;TETR_TEST_BATCH=1;TETR_TEST_MAX_PIECES=1")

    tetr_test_module(util_equivalence_test tetr_core_tests tests/util_equivalence_test.cpp)
    target_link_libraries(util_equivalence_test PRIVATE tetr_util)
    add_test(NAME util_equivalence COMMAND tetr_core_tests util_equivalence_test)

    tetr_test_module(user_config_test tetr_application_tests tests/user_config_test.cpp)
    target_link_libraries(user_config_test PRIVATE tetr_config)
    add_test(NAME user_config COMMAND tetr_application_tests user_config_test)
    tetr_test_module(client_config_test tetr_application_tests tests/client_config_test.cpp)
    target_link_libraries(client_config_test PRIVATE tetr_app Threads::Threads)
    add_test(NAME client_config COMMAND tetr_application_tests client_config_test)
    set_tests_properties(client_config PROPERTIES TIMEOUT 30)

    tetr_test_module(settings_schema_test tetr_application_tests tests/settings_schema_test.cpp)
    target_link_libraries(settings_schema_test PRIVATE tetr_config)
    add_test(NAME settings_schema COMMAND tetr_application_tests settings_schema_test)

    tetr_test_module(runtime_config_test tetr_core_tests tests/runtime_config_test.cpp)
    target_link_libraries(runtime_config_test PRIVATE tetr_runtime)
    add_test(NAME runtime_config COMMAND tetr_core_tests runtime_config_test)

    tetr_test_module(game_context_test tetr_engine_tests tests/game_context_test.cpp)
    target_link_libraries(game_context_test PRIVATE tetr_engine tetr_runtime)
    add_test(NAME game_context COMMAND tetr_engine_tests game_context_test)

    tetr_test_module(garbage_context_test tetr_engine_tests tests/garbage_context_test.cpp)
    target_link_libraries(garbage_context_test PRIVATE tetr_engine tetr_runtime)
    add_test(NAME garbage_context COMMAND tetr_engine_tests garbage_context_test)

    tetr_test_module(garbage_rise_test tetr_engine_tests tests/garbage_rise_test.cpp)
    target_link_libraries(garbage_rise_test PRIVATE tetr_engine tetr_runtime)
    add_test(NAME garbage_rise COMMAND tetr_engine_tests garbage_rise_test)
    set_tests_properties(garbage_rise PROPERTIES TIMEOUT 20)

    tetr_test_module(qp_pressure_test tetr_engine_tests tests/qp_pressure_test.cpp)
    target_link_libraries(qp_pressure_test PRIVATE tetr_engine tetr_runtime)
    add_test(NAME qp_pressure COMMAND tetr_engine_tests qp_pressure_test)
    set_tests_properties(qp_pressure PROPERTIES TIMEOUT 20)

    tetr_test_module(next_preview_render_test tetr_engine_tests tests/next_preview_render_test.cpp)
    target_link_libraries(next_preview_render_test PRIVATE tetr_engine tetr_runtime)
    add_test(NAME next_preview_render COMMAND tetr_engine_tests next_preview_render_test)
    tetr_test_module(replay_garbage_timing_test tetr_engine_tests tests/replay_garbage_timing_test.cpp)
    target_link_libraries(replay_garbage_timing_test PRIVATE tetr_engine tetr_runtime)
    add_test(NAME replay_garbage_timing COMMAND tetr_engine_tests replay_garbage_timing_test)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.tetr/replay/windup-test.txt")
        add_test(NAME windup_saved_replay COMMAND tetr_engine_tests replay_garbage_timing_test
            "${CMAKE_CURRENT_SOURCE_DIR}/.tetr/replay/windup-test.txt")
        set_tests_properties(windup_saved_replay PROPERTIES TIMEOUT 20)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.tetr/replay/bug.txt")
        add_test(NAME quick_play_bot_saved_replay COMMAND tetr_engine_tests replay_garbage_timing_test
            "${CMAKE_CURRENT_SOURCE_DIR}/.tetr/replay/bug.txt")
        set_tests_properties(quick_play_bot_saved_replay PROPERTIES TIMEOUT 30)
    endif()

    tetr_test_module(legacy_bot_boundary_test tetr_bot_tests tests/legacy_bot_boundary_test.cpp)
    target_link_libraries(legacy_bot_boundary_test PRIVATE tetr_runtime tetr_bot_legacy)
    add_test(NAME legacy_bot_boundary COMMAND tetr_bot_tests legacy_bot_boundary_test)

    tetr_test_module(bot_typed_boundary_test tetr_bot_tests tests/bot_typed_boundary_test.cpp)
    target_link_libraries(bot_typed_boundary_test PRIVATE tetr_runtime tetr_bot_legacy tetr_engine_presentation)
    add_test(NAME bot_typed_boundary COMMAND tetr_bot_tests bot_typed_boundary_test)

    tetr_test_module(bot_strategy_isolation_test tetr_bot_tests tests/bot_strategy_isolation_test.cpp)
    target_link_libraries(bot_strategy_isolation_test PRIVATE tetr_runtime tetr_bot_legacy)
    add_test(NAME bot_strategy_isolation COMMAND tetr_bot_tests bot_strategy_isolation_test)

    tetr_test_module(bot_zzztoj_test tetr_bot_tests tests/bot_zzztoj_test.cpp)
    target_link_libraries(bot_zzztoj_test PRIVATE tetr_runtime tetr_bot_legacy)
    add_test(NAME bot_zzztoj COMMAND tetr_bot_tests bot_zzztoj_test)
    tetr_test_module(bot_quick_play_mods_test tetr_bot_tests tests/bot_quick_play_mods_test.cpp)
    target_link_libraries(bot_quick_play_mods_test PRIVATE tetr_bot_legacy tetr_bot_quick_play tetr_config tetr_engine)
    tetr_test_module(bot_quick_play_altitude_benchmark tetr_bot_tests tests/bot_quick_play_altitude_benchmark.cpp)
    target_link_libraries(bot_quick_play_altitude_benchmark PRIVATE tetr_app tetr_bot_legacy tetr_bot_quick_play tetr_config tetr_engine)
    add_test(NAME bot_quick_play_mods COMMAND tetr_bot_tests bot_quick_play_mods_test)
    add_test(NAME bot_chatgpt_capabilities COMMAND tetr_bot_tests bot_quick_play_mods_test --capability-regression)
    set_tests_properties(bot_chatgpt_capabilities PROPERTIES TIMEOUT 120)
    add_test(NAME bot_chatgpt_mod_pairs COMMAND tetr_bot_tests bot_quick_play_mods_test --capability-pairs)
    add_test(NAME bot_chatgpt_spin_objective COMMAND tetr_bot_tests bot_quick_play_mods_test --spin-objective)
    set_tests_properties(bot_chatgpt_mod_pairs PROPERTIES TIMEOUT 180)
    set_tests_properties(bot_quick_play_mods PROPERTIES TIMEOUT 120)
    add_test(NAME bot_quick_play_attack COMMAND tetr_bot_tests bot_quick_play_mods_test --attack-benchmark 100 123 0 300)
    add_test(NAME bot_quick_play_attack_seed COMMAND tetr_bot_tests bot_quick_play_mods_test --attack-benchmark 100 20261014 0 300)
    add_test(NAME bot_quick_play_attack_pressure COMMAND tetr_bot_tests bot_quick_play_mods_test --attack-benchmark 160 20261013 1 300)
    add_test(NAME bot_quick_play_attack_small COMMAND tetr_bot_tests bot_quick_play_mods_test --attack-benchmark 150 123 0 12)
    add_test(NAME bot_quick_play_mod_attack COMMAND tetr_bot_tests bot_quick_play_mods_test --mod-benchmark 100)
    set_tests_properties(bot_quick_play_attack bot_quick_play_attack_seed
        bot_quick_play_attack_pressure bot_quick_play_attack_small bot_quick_play_mod_attack PROPERTIES TIMEOUT 300)
    # These strength checks use real wall-clock search budgets. Competing test
    # processes change the explored trajectories on hosted runners, so retain
    # every attack assertion while giving each benchmark its own CPU interval.
    set_tests_properties(bot_quick_play_attack bot_quick_play_attack_seed
        bot_quick_play_attack_pressure bot_quick_play_attack_small bot_quick_play_mod_attack PROPERTIES RUN_SERIAL TRUE)
    if(MSVC)
        # The unchanged 160-piece, width-300 trajectory exercises all bots;
        # the scalar MSVC build takes longer on hosted Windows runners.
        set_tests_properties(bot_quick_play_attack_pressure PROPERTIES TIMEOUT 900)
    endif()

    tetr_test_module(bot_coldclear_test tetr_bot_tests tests/bot_coldclear_test.cpp)
    target_link_libraries(bot_coldclear_test PRIVATE tetr_bot_coldclear tetr_bot_legacy tetr_rules)
    target_compile_definitions(bot_coldclear_test PRIVATE
        COLDCLEAR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/coldclear_terrain.csv")
    add_test(NAME bot_coldclear COMMAND tetr_bot_tests bot_coldclear_test)
    set_tests_properties(bot_coldclear PROPERTIES TIMEOUT 120)

    tetr_test_module(bot_chatgpt_test tetr_bot_search_tests tests/bot_chatgpt_test.cpp)
    target_include_directories(bot_chatgpt_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt COMMAND tetr_bot_search_tests bot_chatgpt_test)
    set_tests_properties(bot_chatgpt PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_key_piece_test tetr_bot_search_tests tests/bot_chatgpt_key_piece_test.cpp)
    target_include_directories(bot_chatgpt_key_piece_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_key_piece_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_key_piece COMMAND tetr_bot_search_tests bot_chatgpt_key_piece_test)
    tetr_test_module(bot_chatgpt_reachable_holes_test tetr_bot_search_tests tests/bot_chatgpt_reachable_holes_test.cpp)
    target_include_directories(bot_chatgpt_reachable_holes_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_reachable_holes_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_reachable_holes COMMAND tetr_bot_search_tests bot_chatgpt_reachable_holes_test)
    tetr_test_program(bot_chatgpt_known_bag_test tests/bot_chatgpt_known_bag_test.cpp)
    target_link_libraries(bot_chatgpt_known_bag_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    if(TETR_CHATGPT_KNOWN_BAG)
        target_compile_definitions(bot_chatgpt_known_bag_test PRIVATE TETR_CHATGPT_KNOWN_BAG=1)
    endif()
    add_test(NAME bot_chatgpt_known_bag COMMAND bot_chatgpt_known_bag_test)
    set_tests_properties(bot_chatgpt_known_bag PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_o_roof_test tetr_bot_search_tests tests/bot_chatgpt_o_roof_test.cpp)
    target_include_directories(bot_chatgpt_o_roof_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_o_roof_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_o_roof COMMAND tetr_bot_search_tests bot_chatgpt_o_roof_test)
    set_tests_properties(bot_chatgpt_o_roof PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_moch_multi_step_test tetr_bot_search_tests tests/bot_chatgpt_moch_multi_step_test.cpp)
    target_include_directories(bot_chatgpt_moch_multi_step_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_moch_multi_step_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_moch_multi_step COMMAND tetr_bot_search_tests bot_chatgpt_moch_multi_step_test)
    set_tests_properties(bot_chatgpt_moch_multi_step PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_rise_horizon_test tetr_bot_search_tests tests/bot_chatgpt_rise_horizon_test.cpp)
    target_include_directories(bot_chatgpt_rise_horizon_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_rise_horizon_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_rise_horizon COMMAND tetr_bot_search_tests bot_chatgpt_rise_horizon_test)
    set_tests_properties(bot_chatgpt_rise_horizon PROPERTIES TIMEOUT 30)
    tetr_test_program(bot_chatgpt_transposition_test tests/bot_chatgpt_transposition_test.cpp)
    target_include_directories(bot_chatgpt_transposition_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_transposition_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_transposition COMMAND bot_chatgpt_transposition_test)
    set_tests_properties(bot_chatgpt_transposition PROPERTIES TIMEOUT 30)
    tetr_test_program(bot_chatgpt_garbage_rollout_test tests/bot_chatgpt_garbage_rollout_test.cpp)
    target_include_directories(bot_chatgpt_garbage_rollout_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_garbage_rollout_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_garbage_rollout COMMAND bot_chatgpt_garbage_rollout_test)
    set_tests_properties(bot_chatgpt_garbage_rollout PROPERTIES TIMEOUT 30)
    tetr_test_program(bot_chatgpt_root_coverage_test tests/bot_chatgpt_root_coverage_test.cpp)
    target_include_directories(bot_chatgpt_root_coverage_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_root_coverage_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_root_coverage COMMAND bot_chatgpt_root_coverage_test)
    set_tests_properties(bot_chatgpt_root_coverage PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_settled_risk_test tetr_bot_search_tests tests/bot_chatgpt_settled_risk_test.cpp)
    target_include_directories(bot_chatgpt_settled_risk_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_settled_risk_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_settled_risk COMMAND tetr_bot_search_tests bot_chatgpt_settled_risk_test)
    set_tests_properties(bot_chatgpt_settled_risk PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_fast_terrain_test tetr_bot_search_tests tests/bot_chatgpt_fast_terrain_test.cpp)
    target_include_directories(bot_chatgpt_fast_terrain_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_fast_terrain_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_fast_terrain COMMAND tetr_bot_search_tests bot_chatgpt_fast_terrain_test)
    set_tests_properties(bot_chatgpt_fast_terrain PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_terrain_test tetr_bot_search_tests tests/bot_chatgpt_terrain_test.cpp)
    target_include_directories(bot_chatgpt_terrain_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_terrain_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_terrain COMMAND tetr_bot_search_tests bot_chatgpt_terrain_test)
    set_tests_properties(bot_chatgpt_terrain PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_terrain_evaluation_test tetr_bot_search_tests tests/bot_chatgpt_terrain_evaluation_test.cpp)
    target_include_directories(bot_chatgpt_terrain_evaluation_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_terrain_evaluation_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_terrain_evaluation COMMAND tetr_bot_search_tests bot_chatgpt_terrain_evaluation_test)
    set_tests_properties(bot_chatgpt_terrain_evaluation PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_terrain_bridge_test tetr_bot_search_tests tests/bot_chatgpt_terrain_bridge_test.cpp)
    target_include_directories(bot_chatgpt_terrain_bridge_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_terrain_bridge_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_terrain_bridge COMMAND tetr_bot_search_tests bot_chatgpt_terrain_bridge_test)
    set_tests_properties(bot_chatgpt_terrain_bridge PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_learned_terrain_test tetr_bot_search_tests tests/bot_chatgpt_learned_terrain_test.cpp)
    target_include_directories(bot_chatgpt_learned_terrain_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_learned_terrain_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_learned_terrain COMMAND tetr_bot_search_tests bot_chatgpt_learned_terrain_test)
    set_tests_properties(bot_chatgpt_learned_terrain PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_local_pattern_test tetr_bot_search_tests tests/bot_chatgpt_local_pattern_test.cpp)
    target_include_directories(bot_chatgpt_local_pattern_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_local_pattern_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_local_pattern COMMAND tetr_bot_search_tests bot_chatgpt_local_pattern_test)
    set_tests_properties(bot_chatgpt_local_pattern PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_local_pattern_evaluation_test tetr_bot_search_tests tests/bot_chatgpt_local_pattern_evaluation_test.cpp)
    target_include_directories(bot_chatgpt_local_pattern_evaluation_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_local_pattern_evaluation_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_local_pattern_evaluation COMMAND tetr_bot_search_tests bot_chatgpt_local_pattern_evaluation_test)
    set_tests_properties(bot_chatgpt_local_pattern_evaluation PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_time_b2b_evaluation_test tetr_bot_search_tests tests/bot_chatgpt_time_b2b_evaluation_test.cpp)
    target_include_directories(bot_chatgpt_time_b2b_evaluation_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_time_b2b_evaluation_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_time_b2b_evaluation COMMAND tetr_bot_search_tests bot_chatgpt_time_b2b_evaluation_test)
    set_tests_properties(bot_chatgpt_time_b2b_evaluation PROPERTIES TIMEOUT 30)
    add_executable(bot_chatgpt_root_probe EXCLUDE_FROM_ALL src/bot/chatgpt/root_probe.cpp)
    target_include_directories(bot_chatgpt_root_probe PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_root_probe PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_executable(bot_chatgpt_spin_probe EXCLUDE_FROM_ALL src/bot/chatgpt/analysis/spin_probe.cpp)
    target_include_directories(bot_chatgpt_spin_probe PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_spin_probe PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_executable(bot_chatgpt_native_probe EXCLUDE_FROM_ALL src/bot/chatgpt/analysis/native_probe.cpp)
    target_include_directories(bot_chatgpt_native_probe PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_native_probe PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    tetr_test_module(bot_chatgpt_native_test tetr_bot_search_tests tests/bot_chatgpt_native_test.cpp)
    target_include_directories(bot_chatgpt_native_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_native_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_native COMMAND tetr_bot_search_tests bot_chatgpt_native_test)
    set_tests_properties(bot_chatgpt_native PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_hold_context_test tetr_bot_search_tests tests/bot_chatgpt_hold_context_test.cpp)
    target_include_directories(bot_chatgpt_hold_context_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_hold_context_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_hold_context COMMAND tetr_bot_search_tests bot_chatgpt_hold_context_test)
    set_tests_properties(bot_chatgpt_hold_context PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_rise_context_test tetr_bot_search_tests tests/bot_chatgpt_rise_context_test.cpp)
    tetr_test_module(bot_chatgpt_coverage_test tetr_bot_search_tests tests/bot_chatgpt_coverage_test.cpp)
    target_include_directories(bot_chatgpt_coverage_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_coverage_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_coverage COMMAND tetr_bot_search_tests bot_chatgpt_coverage_test)
    set_tests_properties(bot_chatgpt_coverage PROPERTIES TIMEOUT 60)
    target_include_directories(bot_chatgpt_rise_context_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_rise_context_test PRIVATE tetr_bot_legacy tetr_rules tetr_engine_core tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_rise_context COMMAND tetr_bot_search_tests bot_chatgpt_rise_context_test)
    set_tests_properties(bot_chatgpt_rise_context PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_quota_test tetr_bot_search_tests tests/bot_chatgpt_quota_test.cpp)
    target_include_directories(bot_chatgpt_quota_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_quota_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_quota COMMAND tetr_bot_search_tests bot_chatgpt_quota_test)
    set_tests_properties(bot_chatgpt_quota PROPERTIES TIMEOUT 30)
    tetr_test_program(bot_chatgpt_beam_quota_test tests/bot_chatgpt_beam_quota_test.cpp)
    target_include_directories(bot_chatgpt_beam_quota_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_beam_quota_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_beam_quota COMMAND bot_chatgpt_beam_quota_test)
    set_tests_properties(bot_chatgpt_beam_quota PROPERTIES TIMEOUT 30)
    tetr_test_module(bot_chatgpt_spin_test tetr_bot_search_tests tests/bot_chatgpt_spin_test.cpp)
    target_include_directories(bot_chatgpt_spin_test PRIVATE zzztoj_standalone/src)
    target_link_libraries(bot_chatgpt_spin_test PRIVATE tetr_bot_legacy tetr_rules tetr_bot_zzztoj_upstream)
    add_test(NAME bot_chatgpt_spin COMMAND tetr_bot_search_tests bot_chatgpt_spin_test)
    set_tests_properties(bot_chatgpt_spin PROPERTIES TIMEOUT 30)

    tetr_test_module(bot_zzztoj_benchmark tetr_bot_tests tests/bot_zzztoj_benchmark.cpp)
    target_link_libraries(bot_zzztoj_benchmark
        PRIVATE tetr_engine_core tetr_rules tetr_runtime tetr_bot_legacy)
    add_test(NAME bot_zzztoj_opening_quality
        COMMAND tetr_bot_tests bot_zzztoj_benchmark 7 300 20260918 3)
    add_test(NAME bot_zzztoj_survival
        COMMAND tetr_bot_tests bot_zzztoj_benchmark 300 300 20260919 18 botris)
    foreach(zzztoj_variant IN ITEMS toj dig qq c2)
        add_test(NAME "bot_zzztoj_${zzztoj_variant}_survival"
            COMMAND tetr_bot_tests bot_zzztoj_benchmark 100 300 20260919 -1 "${zzztoj_variant}")
        set_tests_properties("bot_zzztoj_${zzztoj_variant}_survival"
            PROPERTIES TIMEOUT 30)
    endforeach()
    set_tests_properties(bot_zzztoj_opening_quality PROPERTIES TIMEOUT 10)
    set_tests_properties(bot_zzztoj_survival PROPERTIES TIMEOUT 30)

    tetr_test_module(init_context_test tetr_application_tests tests/init_context_test.cpp)
    target_link_libraries(init_context_test PRIVATE tetr_app tetr_engine tetr_runtime)
    add_test(NAME init_context COMMAND tetr_application_tests init_context_test)

    tetr_test_module(local_versus_init_test tetr_application_tests tests/local_versus_init_test.cpp)
    target_link_libraries(local_versus_init_test PRIVATE tetr_app tetr_engine tetr_runtime tetr_network)
    add_test(NAME local_versus_init COMMAND tetr_application_tests local_versus_init_test)

    tetr_test_module(clutch_test tetr_application_tests tests/clutch_test.cpp)
    target_link_libraries(clutch_test PRIVATE tetr_engine tetr_runtime)
    add_test(NAME clutch COMMAND tetr_application_tests clutch_test)

    tetr_test_module(render_layer_test tetr_engine_tests tests/render_layer_test.cpp)
    target_link_libraries(render_layer_test PRIVATE tetr_render tetr_runtime)
    add_test(NAME render_layer COMMAND tetr_engine_tests render_layer_test)


    tetr_test_module(player_session_state_test tetr_core_tests tests/player_session_state_test.cpp)
    target_link_libraries(player_session_state_test PRIVATE tetr_runtime)
    add_test(NAME player_session_state COMMAND tetr_core_tests player_session_state_test)

    tetr_test_module(piece_queue_equivalence_test tetr_rules_tests tests/piece_queue_equivalence_test.cpp)
    target_link_libraries(piece_queue_equivalence_test PRIVATE tetr_engine_core tetr_runtime)
    add_test(NAME piece_queue_equivalence COMMAND tetr_rules_tests piece_queue_equivalence_test)

    tetr_test_module(bot_bag_remainder_test tetr_bot_tests tests/bot_bag_remainder_test.cpp)
    target_link_libraries(bot_bag_remainder_test PRIVATE tetr_engine_core tetr_runtime)
    add_test(NAME bot_bag_remainder COMMAND tetr_bot_tests bot_bag_remainder_test)

    tetr_test_module(board_state_codec_test tetr_network_tests tests/board_state_codec_test.cpp)
    target_link_libraries(board_state_codec_test PRIVATE tetr_network tetr_runtime)
    add_test(NAME board_state_codec COMMAND tetr_network_tests board_state_codec_test)

    tetr_test_module(board_state_receive_codec_test tetr_network_tests tests/board_state_receive_codec_test.cpp)
    target_link_libraries(board_state_receive_codec_test PRIVATE tetr_network tetr_runtime tetr_rules)
    add_test(NAME board_state_receive_codec COMMAND tetr_network_tests board_state_receive_codec_test)

    tetr_test_module(strong_types_test tetr_core_tests tests/strong_types_test.cpp)
    target_link_libraries(strong_types_test PRIVATE tetr_headers)
    add_test(NAME strong_types COMMAND tetr_core_tests strong_types_test)

    tetr_test_module(input_semantics_test tetr_engine_tests tests/input_semantics_test.cpp)
    target_link_libraries(input_semantics_test PRIVATE tetr_engine)
    add_test(NAME input_semantics COMMAND tetr_engine_tests input_semantics_test)
    if(WIN32)
        tetr_test_program(windows_console_focus_test tests/windows_console_focus_test.cpp)
        target_link_libraries(windows_console_focus_test PRIVATE tetr_headers Threads::Threads)
        add_test(NAME windows_console_focus COMMAND windows_console_focus_test)
        tetr_test_program(windows_multiplayer_latency_benchmark tests/windows_multiplayer_latency_benchmark.cpp)
        target_link_libraries(windows_multiplayer_latency_benchmark PRIVATE tetr_app tetr_network)
    endif()

    tetr_test_module(time_semantics_test tetr_engine_tests tests/time_semantics_test.cpp)
    target_link_libraries(time_semantics_test PRIVATE tetr_engine)
    add_test(NAME time_semantics COMMAND tetr_engine_tests time_semantics_test)

    tetr_test_module(serialization_boundary_test tetr_application_tests tests/serialization_boundary_test.cpp)
    target_link_libraries(serialization_boundary_test PRIVATE tetr_engine tetr_network tetr_runtime tetr_rules)
    add_test(NAME serialization_boundary COMMAND tetr_application_tests serialization_boundary_test)

    tetr_test_module(multiplayer_frame_boundary_test tetr_engine_tests tests/multiplayer_frame_boundary_test.cpp)
    target_link_libraries(multiplayer_frame_boundary_test PRIVATE tetr_engine tetr_network tetr_runtime tetr_rules)
    add_test(NAME multiplayer_frame_boundary COMMAND tetr_engine_tests multiplayer_frame_boundary_test)

    tetr_test_module(multiplayer_lobby_protocol_test tetr_network_tests tests/multiplayer_lobby_protocol_test.cpp)
    target_link_libraries(multiplayer_lobby_protocol_test PRIVATE tetr_network tetr_runtime)
    add_test(NAME multiplayer_lobby_protocol COMMAND tetr_network_tests multiplayer_lobby_protocol_test)

    tetr_test_module(network_wait_test tetr_network_tests tests/network_wait_test.cpp)
    target_link_libraries(network_wait_test PRIVATE tetr_network)
    add_test(NAME network_wait COMMAND tetr_network_tests network_wait_test)
    tetr_test_module(hybrid_board_sync_test tetr_network_tests tests/hybrid_board_sync_test.cpp)
    target_link_libraries(hybrid_board_sync_test PRIVATE tetr_engine tetr_network tetr_runtime)
    add_test(NAME hybrid_board_sync COMMAND tetr_network_tests hybrid_board_sync_test)

    tetr_test_module(network_rooms_test tetr_network_tests tests/network_rooms_test.cpp)
    target_link_libraries(network_rooms_test PRIVATE tetr_network tetr_runtime)
    add_test(NAME network_rooms COMMAND tetr_network_tests network_rooms_test)
    tetr_test_module(room_kick_test tetr_network_tests tests/room_kick_test.cpp)
    target_link_libraries(room_kick_test PRIVATE tetr_app tetr_network)
    add_test(NAME room_kick COMMAND tetr_network_tests room_kick_test)
    set_tests_properties(room_kick PROPERTIES TIMEOUT 35 RESOURCE_LOCK udp_server_3007)
    tetr_test_module(room_names_test tetr_network_tests tests/room_names_test.cpp)
    target_link_libraries(room_names_test PRIVATE tetr_network tetr_runtime)
    add_test(NAME room_names COMMAND tetr_network_tests room_names_test)
    set_tests_properties(room_names PROPERTIES TIMEOUT 30 RESOURCE_LOCK udp_server_3018)
    add_executable(multiplayer_bot_gallery tools/multiplayer_bot_gallery.cpp)
    target_link_libraries(multiplayer_bot_gallery PRIVATE tetr_app tetr_network tetr_bot_legacy Threads::Threads)
    tetr_test_module(multiplayer_rooms_test tetr_network_tests tests/multiplayer_rooms_test.cpp)
    target_link_libraries(multiplayer_rooms_test PRIVATE tetr_app tetr_network)
    add_test(NAME multiplayer_rooms COMMAND tetr_network_tests multiplayer_rooms_test)
    tetr_test_module(multiplayer_scene_test tetr_network_tests tests/multiplayer_scene_test.cpp)
    target_link_libraries(multiplayer_scene_test PRIVATE tetr_app tetr_network)
    add_test(NAME multiplayer_scene COMMAND tetr_network_tests multiplayer_scene_test)
    tetr_test_module(room_preparation_test tetr_network_tests tests/room_preparation_test.cpp)
    target_link_libraries(room_preparation_test PRIVATE tetr_app tetr_network)
    add_test(NAME room_preparation COMMAND tetr_network_tests room_preparation_test)
    set_tests_properties(room_preparation PROPERTIES TIMEOUT 30 RESOURCE_LOCK udp3034)
    tetr_test_module(room_config_burst_test tetr_network_tests tests/room_config_burst_test.cpp)
    target_link_libraries(room_config_burst_test PRIVATE tetr_network)
    add_test(NAME room_config_burst COMMAND tetr_network_tests room_config_burst_test)
    set_tests_properties(room_config_burst PROPERTIES TIMEOUT 15 RESOURCE_LOCK udp3038)
    tetr_test_module(room_late_spectator_test tetr_network_tests tests/room_late_spectator_test.cpp)
    target_link_libraries(room_late_spectator_test PRIVATE tetr_app tetr_network)
    add_test(NAME room_late_spectator COMMAND tetr_network_tests room_late_spectator_test)
    set_tests_properties(room_late_spectator PROPERTIES TIMEOUT 30 RESOURCE_LOCK udp_server_3035)
    if(UNIX)
        tetr_test_module(room_start_spectator_test tetr_network_tests tests/room_start_spectator_test.cpp)
        target_link_libraries(room_start_spectator_test PRIVATE tetr_app tetr_network)
        add_test(NAME room_start_spectator COMMAND tetr_network_tests room_start_spectator_test)
        add_test(NAME room_start_spectator_8 COMMAND tetr_network_tests room_start_spectator_test --local 8 1)
        set_tests_properties(room_start_spectator room_start_spectator_8 PROPERTIES TIMEOUT 40 RESOURCE_LOCK udp3040)
    endif()
    tetr_test_module(multiplayer_geometry_test tetr_network_tests tests/multiplayer_geometry_test.cpp)
    target_link_libraries(multiplayer_geometry_test PRIVATE tetr_app tetr_network)
    add_test(NAME multiplayer_geometry COMMAND tetr_network_tests multiplayer_geometry_test)
    tetr_test_program(adaptive_board_view_test tests/adaptive_board_view_test.cpp)
    target_link_libraries(adaptive_board_view_test PRIVATE tetr_app tetr_network)
    add_test(NAME adaptive_board_view COMMAND adaptive_board_view_test)
    set_tests_properties(adaptive_board_view PROPERTIES TIMEOUT 40 RESOURCE_LOCK udp3026)
    tetr_test_module(board_view_stagger_test tetr_network_tests tests/board_view_stagger_test.cpp)
    target_link_libraries(board_view_stagger_test PRIVATE tetr_app tetr_network)
    add_test(NAME board_view_stagger COMMAND tetr_network_tests board_view_stagger_test)
    set_tests_properties(board_view_stagger PROPERTIES TIMEOUT 40 RESOURCE_LOCK udp3027)
    add_test(NAME board_view_stagger_32 COMMAND tetr_network_tests board_view_stagger_test 32)
    set_tests_properties(board_view_stagger_32 PROPERTIES TIMEOUT 40 RESOURCE_LOCK udp3027)
    add_test(NAME board_view_steady_8 COMMAND tetr_network_tests board_view_stagger_test 8 --steady)
    # Measure the relay's real 30 Hz cadence without competing Bot benchmarks.
    set_tests_properties(board_view_steady_8 PROPERTIES TIMEOUT 40 RESOURCE_LOCK udp3027 RUN_SERIAL TRUE)

    tetr_test_program(multiplayer_round_lifecycle_test tests/multiplayer_round_lifecycle_test.cpp)
    target_link_libraries(multiplayer_round_lifecycle_test PRIVATE tetr_app tetr_network)
    add_test(NAME multiplayer_round_lifecycle COMMAND multiplayer_round_lifecycle_test)
    set_tests_properties(multiplayer_round_lifecycle PROPERTIES RESOURCE_LOCK udp_server_3017)
    set_tests_properties(multiplayer_round_lifecycle PROPERTIES TIMEOUT 45 ENVIRONMENT "TETR_TEST_CLOCK_SCALE=10")
    set_tests_properties(multiplayer_rooms PROPERTIES TIMEOUT 45 RESOURCE_LOCK udp_server_3005)
    tetr_test_module(multiplayer_config_test tetr_network_tests tests/multiplayer_config_test.cpp)
    target_link_libraries(multiplayer_config_test PRIVATE tetr_config tetr_network)
    add_test(NAME multiplayer_config COMMAND tetr_network_tests multiplayer_config_test)
    tetr_test_program(room_host_config_test tests/room_host_config_test.cpp)
    target_link_libraries(room_host_config_test PRIVATE tetr_app tetr_network)
    add_test(NAME room_host_config COMMAND room_host_config_test)
    set_tests_properties(network_rooms PROPERTIES TIMEOUT 30 RESOURCE_LOCK udp_server_3002)
    tetr_test_module(network_connection_test tetr_network_tests tests/network_connection_test.cpp)
    target_link_libraries(network_connection_test PRIVATE tetr_network tetr_runtime)
    add_test(NAME network_connection COMMAND tetr_network_tests network_connection_test)
    set_tests_properties(network_connection PROPERTIES TIMEOUT 35 RESOURCE_LOCK udp_server_3000)
    if(WIN32)
        # Windows' select() timer granularity makes the relay pump slower.
        set_tests_properties(network_connection PROPERTIES TIMEOUT 45)
    endif()
    tetr_test_program(network_websocket_test tests/network_websocket_test.cpp)
    target_link_libraries(network_websocket_test PRIVATE tetr_network tetr_runtime)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(NAME tunnel_watchdog COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/tunnel_watchdog_test.py)
            set_tests_properties(tunnel_watchdog PROPERTIES TIMEOUT 15)
        endif()
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import websockets"
            RESULT_VARIABLE _tetr_has_websockets OUTPUT_QUIET ERROR_QUIET)
        if(_tetr_has_websockets EQUAL 0)
            add_test(NAME network_websocket COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/network_websocket_test.py
                $<TARGET_FILE:network_websocket_test> $<TARGET_FILE:tetr_server> $<TARGET_FILE:room_host_config_test>
                $<TARGET_FILE:adaptive_board_view_test>)
            set_tests_properties(network_websocket PROPERTIES TIMEOUT 90 RESOURCE_LOCK udp_server_3000)
            add_test(NAME multiplayer_countdown_websocket COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/multiplayer_countdown_websocket_test.py
                $<TARGET_FILE:multiplayer_round_lifecycle_test>)
            set_tests_properties(multiplayer_countdown_websocket PROPERTIES TIMEOUT 50
                ENVIRONMENT "TETR_TEST_CLOCK_SCALE=10" RESOURCE_LOCK udp_server_3017)
            add_test(NAME room_replays_websocket COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/room_replays_websocket_test.py $<TARGET_FILE:tetr_network_tests>)
            set_tests_properties(room_replays_websocket PROPERTIES TIMEOUT 110 RESOURCE_LOCK udp_server_3045)
        else()
            message(STATUS "WebSocket integration test requires deploy/websocket-requirements.txt")
        endif()
    endif()

    tetr_test_module(config_boundary_test tetr_application_tests tests/config_boundary_test.cpp)
    target_link_libraries(config_boundary_test PRIVATE tetr_engine tetr_runtime tetr_config)
    add_test(NAME config_boundary COMMAND tetr_application_tests config_boundary_test)

    tetr_test_module(piece_catalog_test tetr_core_tests tests/piece_catalog_test.cpp)
    target_link_libraries(piece_catalog_test PRIVATE tetr_runtime)
    add_test(NAME piece_catalog COMMAND tetr_core_tests piece_catalog_test)

    tetr_test_module(startup_catalog_test tetr_core_tests tests/startup_catalog_test.cpp)
    target_link_libraries(startup_catalog_test PRIVATE tetr_runtime)
    add_test(NAME startup_catalog COMMAND tetr_core_tests startup_catalog_test)

    tetr_test_module(bot_selection_test tetr_bot_tests tests/bot_selection_test.cpp)
    target_link_libraries(bot_selection_test PRIVATE tetr_app tetr_config tetr_runtime)
    add_test(NAME bot_selection COMMAND tetr_bot_tests bot_selection_test)

    tetr_test_module(disabled_movement_test tetr_application_tests tests/disabled_movement_test.cpp)
    target_link_libraries(disabled_movement_test PRIVATE tetr_app tetr_bot_legacy)
    add_test(NAME disabled_movement COMMAND tetr_application_tests disabled_movement_test)
    set_tests_properties(disabled_movement PROPERTIES TIMEOUT 60)

    tetr_test_module(simulation_time_test tetr_application_tests tests/simulation_time_test.cpp)
    target_link_libraries(simulation_time_test PRIVATE tetr_engine tetr_config)
    add_test(NAME simulation_time COMMAND tetr_application_tests simulation_time_test)
    set_tests_properties(simulation_time PROPERTIES TIMEOUT 20)

    tetr_test_module(turn_replay_test tetr_application_tests tests/turn_replay_test.cpp)
    target_link_libraries(turn_replay_test PRIVATE tetr_engine tetr_config)
    add_test(NAME turn_replay COMMAND tetr_application_tests turn_replay_test)
    set_tests_properties(turn_replay PROPERTIES TIMEOUT 20)

    tetr_test_module(settings_menu_test tetr_application_tests tests/settings_menu_test.cpp)
    target_link_libraries(settings_menu_test PRIVATE tetr_app tetr_config)
    add_test(NAME settings_menu COMMAND tetr_application_tests settings_menu_test)
    tetr_test_module(map_editor_test tetr_application_tests tests/map_editor_test.cpp)
    target_link_libraries(map_editor_test PRIVATE tetr_app)
    add_test(NAME map_editor COMMAND tetr_application_tests map_editor_test)
    set_tests_properties(map_editor PROPERTIES TIMEOUT 30)
    tetr_test_module(game_goals_test tetr_application_tests tests/game_goals_test.cpp)
    target_link_libraries(game_goals_test PRIVATE tetr_app)
    add_test(NAME game_goals COMMAND tetr_application_tests game_goals_test)
    tetr_test_module(field_visibility_test tetr_application_tests tests/field_visibility_test.cpp)
    target_link_libraries(field_visibility_test PRIVATE tetr_app)
    add_test(NAME field_visibility COMMAND tetr_application_tests field_visibility_test)
    set_tests_properties(game_goals PROPERTIES TIMEOUT 20)
    # Includes held-key and cancellation scripts for every preset action.
    # Confirmation holds and rendering make the complete Windows UI suite slower.
    if(WIN32)
        set_tests_properties(settings_menu PROPERTIES TIMEOUT 120)
    else()
        set_tests_properties(settings_menu PROPERTIES TIMEOUT 60)
    endif()
    tetr_test_module(settings_targets_test tetr_application_tests tests/settings_targets_test.cpp)
    target_link_libraries(settings_targets_test PRIVATE tetr_app)
    add_test(NAME settings_targets COMMAND tetr_application_tests settings_targets_test)
    tetr_test_module(autoplay_handling_test tetr_application_tests tests/autoplay_handling_test.cpp)
    target_link_libraries(autoplay_handling_test PRIVATE tetr_app tetr_bot_legacy)
    add_test(NAME autoplay_handling COMMAND tetr_application_tests autoplay_handling_test)
    set_tests_properties(autoplay_handling PROPERTIES TIMEOUT 15)
    tetr_test_module(autoplay_network_test tetr_application_tests tests/autoplay_network_test.cpp)
    target_link_libraries(autoplay_network_test PRIVATE tetr_app tetr_network)
    add_test(NAME autoplay_network COMMAND tetr_application_tests autoplay_network_test)
    set_tests_properties(autoplay_network PROPERTIES TIMEOUT 40 RESOURCE_LOCK udp3038)
    tetr_test_module(autoplay_parameters_test tetr_application_tests tests/autoplay_parameters_test.cpp)
    target_link_libraries(autoplay_parameters_test PRIVATE tetr_app tetr_bot_legacy)
    add_test(NAME autoplay_parameters COMMAND tetr_application_tests autoplay_parameters_test)
    set_tests_properties(autoplay_parameters PROPERTIES TIMEOUT 180)

    tetr_test_program(autoplay_timing_test tests/autoplay_timing_test.cpp)
    target_link_libraries(autoplay_timing_test PRIVATE tetr_app tetr_bot_legacy)
    add_test(NAME autoplay_timing COMMAND autoplay_timing_test)
    # This suite checks real-time cadence; concurrent Bot benchmarks can
    # delay scheduler wakeups beyond its deliberately tight timing bounds.
    set_tests_properties(autoplay_timing PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    tetr_test_module(presets_test tetr_application_tests tests/presets_test.cpp)
    target_link_libraries(presets_test PRIVATE tetr_app)
    add_test(NAME presets COMMAND tetr_application_tests presets_test)
    set_tests_properties(presets PROPERTIES TIMEOUT 10)
    tetr_test_module(settings_presentation_test tetr_application_tests tests/settings_presentation_test.cpp)
    target_link_libraries(settings_presentation_test PRIVATE tetr_app)
    add_test(NAME settings_presentation COMMAND tetr_application_tests settings_presentation_test)

    tetr_test_module(start_screen_navigation_test tetr_application_tests tests/start_screen_navigation_test.cpp)
    target_link_libraries(start_screen_navigation_test PRIVATE tetr_app)
    add_test(NAME start_screen_navigation COMMAND tetr_application_tests start_screen_navigation_test)
    set_tests_properties(start_screen_navigation PROPERTIES TIMEOUT 10)
    tetr_test_module(hold_rules_test tetr_application_tests tests/hold_rules_test.cpp)
    target_link_libraries(hold_rules_test PRIVATE tetr_app)
    add_test(NAME hold_rules COMMAND tetr_application_tests hold_rules_test)
    set_tests_properties(hold_rules PROPERTIES TIMEOUT 20)
    tetr_test_module(post_game_statistics_test tetr_application_tests tests/post_game_statistics_test.cpp)
    tetr_test_module(challenges_test tetr_application_tests tests/challenges_test.cpp)
    tetr_test_module(challenge_prefix_test tetr_application_tests tests/challenge_prefix_test.cpp)
    tetr_test_module(challenge_modes_test tetr_application_tests tests/challenge_modes_test.cpp)
    tetr_test_program(challenge_catalog_test tests/challenge_catalog_test.cpp)
    target_link_libraries(challenge_catalog_test PRIVATE tetr_app tetr_bot_legacy)
    add_test(NAME challenge_catalog COMMAND challenge_catalog_test)
    if(Python3_Interpreter_FOUND)
        add_test(NAME frontend_terminal_baseline COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_baseline_test.py $<TARGET_FILE:challenge_catalog_test>)
        set_tests_properties(frontend_terminal_baseline PROPERTIES TIMEOUT 15)
    endif()
    set_tests_properties(challenge_catalog PROPERTIES TIMEOUT 60)
    target_link_libraries(challenge_modes_test PRIVATE tetr_app tetr_bot_legacy)
    add_test(NAME challenge_modes COMMAND tetr_application_tests challenge_modes_test)
    set_tests_properties(challenge_modes PROPERTIES TIMEOUT 20)
    target_link_libraries(challenge_prefix_test PRIVATE tetr_app)
    add_test(NAME challenge_prefix COMMAND tetr_application_tests challenge_prefix_test)
    set_tests_properties(challenge_prefix PROPERTIES TIMEOUT 20)
    tetr_test_module(challenge_replay_test tetr_application_tests tests/challenge_replay_test.cpp)
    target_link_libraries(challenge_replay_test PRIVATE tetr_app)
    add_test(NAME challenge_replay COMMAND tetr_application_tests challenge_replay_test)
    set_tests_properties(challenge_replay PROPERTIES TIMEOUT 20)
    target_link_libraries(challenges_test PRIVATE tetr_app)
    add_test(NAME challenges COMMAND tetr_application_tests challenges_test)
    set_tests_properties(challenges PROPERTIES TIMEOUT 20)
    target_link_libraries(post_game_statistics_test PRIVATE tetr_app)
    add_test(NAME post_game_statistics COMMAND tetr_application_tests post_game_statistics_test)
    set_tests_properties(post_game_statistics PROPERTIES TIMEOUT 20)
    tetr_test_module(room_results_view_test tetr_network_tests tests/room_results_view_test.cpp)
    tetr_test_module(room_replays_test tetr_network_tests tests/room_replays_test.cpp)
    target_link_libraries(room_replays_test PRIVATE tetr_app tetr_network)
    add_test(NAME room_replays COMMAND tetr_network_tests room_replays_test)
    set_tests_properties(room_replays PROPERTIES TIMEOUT 120 RESOURCE_LOCK udp_server_3045)
    target_link_libraries(room_results_view_test PRIVATE tetr_app tetr_network)
    add_test(NAME room_results_view COMMAND tetr_network_tests room_results_view_test)
    set_tests_properties(room_results_view PROPERTIES TIMEOUT 10)
    tetr_test_module(ui_style_gallery_test tetr_application_tests tests/ui_style_gallery_test.cpp)
    target_link_libraries(ui_style_gallery_test PRIVATE tetr_app tetr_network)
    add_test(NAME ui_style_gallery COMMAND tetr_application_tests ui_style_gallery_test)
    set_tests_properties(ui_style_gallery PROPERTIES TIMEOUT 20)

    tetr_test_module(recent_replays_test tetr_application_tests tests/recent_replays_test.cpp)
    target_link_libraries(recent_replays_test PRIVATE tetr_replay Threads::Threads)
    add_test(NAME recent_replays COMMAND tetr_application_tests recent_replays_test)
    tetr_test_module(timing_garbage_rules_test tetr_application_tests tests/timing_garbage_rules_test.cpp)
    target_link_libraries(timing_garbage_rules_test PRIVATE tetr_app)
    add_test(NAME timing_garbage_rules COMMAND tetr_application_tests timing_garbage_rules_test)
    set_tests_properties(timing_garbage_rules PROPERTIES TIMEOUT 20)

    add_test(
        NAME architecture_boundary
        COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture_boundary_test.cmake)


endif()

if(BUILD_TESTING)
    tetr_finalize_test_suites()
endif()

# Keep an asInvoker manifest with every toolchain so executable names such as
# frame_update_test do not trigger installer detection.
if(WIN32 AND NOT MSVC AND NOT TETR_TOOLCHAIN_PROVIDES_MANIFEST)
    get_property(_tetr_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_tetr_target IN LISTS _tetr_targets)
        get_target_property(_tetr_type ${_tetr_target} TYPE)
        if(_tetr_type STREQUAL "EXECUTABLE")
            target_sources(${_tetr_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows_application.rc")
        endif()
    endforeach()
endif()
