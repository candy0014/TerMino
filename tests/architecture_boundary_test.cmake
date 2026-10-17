if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

function(read_source FILE OUT_VAR)
    file(READ "${FILE}" CONTENT)
    set(${OUT_VAR} "${CONTENT}" PARENT_SCOPE)
endfunction()

function(assert_not_contains FILE NEEDLE REASON)
    read_source("${FILE}" CONTENT)
    string(FIND "${CONTENT}" "${NEEDLE}" POSITION)
    if(NOT POSITION EQUAL -1)
        file(RELATIVE_PATH RELATIVE "${ROOT}" "${FILE}")
        message(FATAL_ERROR "${RELATIVE}: ${REASON}: ${NEEDLE}")
    endif()
endfunction()

# Frontend contracts and pure presentation are buildable without a terminal,
# platform input, App, Replay, Network or graphics libraries. The context
# adapter alone may obtain the common timeline through Replay.
file(GLOB FRONTEND_CONTRACTS "${ROOT}/include/tetr/frontend/*.h")
file(GLOB PURE_PRESENTATION
    "${ROOT}/include/tetr/presentation/visibility.h"
    "${ROOT}/include/tetr/presentation/board_view.h"
    "${ROOT}/src/presentation/visibility.cpp"
    "${ROOT}/src/presentation/board_view.cpp")
foreach(FILE IN LISTS FRONTEND_CONTRACTS PURE_PRESENTATION)
    foreach(FORBIDDEN "tetr/render/" "tetr/app/" "tetr/network/" "tetr/replay/"
            "tetr/input/services.h" "SDL" "<GL/" "<windows.h>" "<linux/input.h>")
        assert_not_contains("${FILE}" "${FORBIDDEN}" "frontend contract/presentation must remain platform independent")
    endforeach()
endforeach()
assert_not_contains("${ROOT}/include/tetr/app/application_state.h" "Render::TerminalSession" "App must reference a frontend text-input interface")


# Public/private headers with the same relative path are ambiguous because the
# include search order can silently select different APIs in different targets.
file(GLOB_RECURSE PRIVATE_HEADERS "${ROOT}/src/*.h")
foreach(FILE IN LISTS PRIVATE_HEADERS)
    file(RELATIVE_PATH RELATIVE "${ROOT}/src" "${FILE}")
    if(EXISTS "${ROOT}/include/tetr/${RELATIVE}")
        message(FATAL_ERROR "duplicate public/private header: ${RELATIVE}")
    endif()
endforeach()

file(GLOB_RECURSE RUNTIME_FILES
    "${ROOT}/include/tetr/runtime/*.h"
    "${ROOT}/src/runtime/*.cpp"
    "${ROOT}/src/runtime/*.h")
set(RUNTIME_FORBIDDEN_LAYERS app autoplay engine input legacy network render replay rules)
foreach(FILE IN LISTS RUNTIME_FILES)
    foreach(LAYER IN LISTS RUNTIME_FORBIDDEN_LAYERS)
        assert_not_contains("${FILE}" "#include \"tetr/${LAYER}/" "runtime must remain a bottom-layer dependency")
    endforeach()
endforeach()

file(GLOB_RECURSE RENDER_FILES
    "${ROOT}/include/tetr/render/*.h"
    "${ROOT}/src/render/*.cpp"
    "${ROOT}/src/render/*.h")
foreach(FILE IN LISTS RENDER_FILES)
    assert_not_contains("${FILE}" "#include \"tetr/network/" "render must not apply or decode network state")
endforeach()

file(GLOB_RECURSE NORMAL_FILES
    "${ROOT}/include/tetr/*.h"
    "${ROOT}/src/*.cpp"
    "${ROOT}/src/*.h")
foreach(FILE IN LISTS NORMAL_FILES)
    string(FIND "${FILE}" "/bot/" BOT_POSITION)
    string(FIND "${FILE}" "/legacy/" LEGACY_POSITION)
    if(BOT_POSITION EQUAL -1 AND LEGACY_POSITION EQUAL -1)
        foreach(SYMBOL
                default_game_context
                default_session_state
                default_runtime_services
                cnt_backfire
                tim_backfire
                flag_hole
                ReplayEvent
                holeNum)
            assert_not_contains("${FILE}" "${SYMBOL}" "normal code must not depend on removed legacy/global state")
        endforeach()
    endif()
endforeach()



# Engine consumes the abstract Autoplay strategy only.  The legacy BOT is
# wired at the application composition root and must never leak back into the
# normal engine layer.
file(GLOB_RECURSE ENGINE_FILES
    "${ROOT}/include/tetr/engine/*.h"
    "${ROOT}/src/engine/*.cpp"
    "${ROOT}/src/engine/*.h")
foreach(FILE IN LISTS ENGINE_FILES)
    assert_not_contains("${FILE}" "#include \"tetr/legacy/" "engine must not depend directly on the legacy BOT")
endforeach()
assert_not_contains("${ROOT}/CMakeLists.txt" "target_link_libraries(tetr_engine PRIVATE tetr_bot_legacy)" "engine must not link the legacy BOT")

# Input/platform services no longer belong to Runtime.  Keep the old include
# paths dead so future changes cannot silently rebuild the former service bag.
file(GLOB_RECURSE SOURCE_AND_HEADERS
    "${ROOT}/include/tetr/*.h"
    "${ROOT}/src/*.cpp"
    "${ROOT}/src/*.h")
foreach(FILE IN LISTS SOURCE_AND_HEADERS)
    assert_not_contains("${FILE}" "tetr/runtime/input_action.h" "typed input belongs to tetr/input")
    assert_not_contains("${FILE}" "tetr/runtime/keyboard_monitor.h" "platform input belongs to tetr/input")
endforeach()

# Runtime gameplay state must not regress to packed/sentinel compatibility
# layouts.  Those encodings are permitted only in explicit legacy/codec code.
file(GLOB_RECURSE NORMAL_STATE_FILES
    "${ROOT}/include/tetr/runtime/*.h"
    "${ROOT}/src/runtime/*.cpp"
    "${ROOT}/src/rules/*.cpp"
    "${ROOT}/src/engine/*.cpp")
foreach(FILE IN LISTS NORMAL_STATE_FILES)
    foreach(SYMBOL QPLastOperation ExtendedAllSpinOperation lastHole)
        assert_not_contains("${FILE}" "${SYMBOL}" "normal state must keep semantic QP/garbage types")
    endforeach()
endforeach()

# Settings labels are presentation text only, never stable program identity.
assert_not_contains("${ROOT}/src/app/settings_menu.cpp" ".label == \"ObscuredColors\"" "settings logic must use SettingId")

# Garbage display cache is presentation state, not gameplay garbage state.
assert_not_contains("${ROOT}/src/engine/garbage_buffer_adapter.cpp" "player.garbage.display" "garbage display cache belongs to presentation state")

# Stage 96+: configuration is its own layer and must not leak back into Runtime.
foreach(FILE IN LISTS RUNTIME_FILES)
    assert_not_contains("${FILE}" "#include \"tetr/config/" "runtime must not depend on configuration IO")
endforeach()

# Replay known operations are typed. Historical name/argument vectors may only
# survive inside UnknownPayload and the codec implementation.
assert_not_contains("${ROOT}/include/tetr/replay/operation.h" "std::string name;\n    Runtime::Duration" "known replay operations must not store protocol names")
assert_not_contains("${ROOT}/include/tetr/replay/operation.h" "std::vector<std::string> arguments;\n};\n\nstruct Operation" "known replay operations must not store positional arguments")

# QP gameplay rules must not reconstruct the historical packed integer format.
assert_not_contains("${ROOT}/src/rules/qp_post_lock.cpp" "8191" "QP rules must use semantic state")
assert_not_contains("${ROOT}/src/rules/qp_post_lock.cpp" "encode_qp" "QP packed encoding belongs to legacy/codec boundaries")
assert_not_contains("${ROOT}/src/rules/qp_post_lock.cpp" "decode_qp" "QP packed encoding belongs to legacy/codec boundaries")

# Stage 106: BOT geometry/state use the same typed Runtime/Piece structures as
# the engine. The old map snapshot, Board facade, and PPP catalog projection
# must not return.
foreach(REMOVED_FILE
        "${ROOT}/include/tetr/map.h"
        "${ROOT}/include/tetr/legacy/board_facade.h"
        "${ROOT}/src/legacy/board_facade.cpp"
        "${ROOT}/include/tetr/legacy/ppp_catalog.h"
        "${ROOT}/src/legacy/ppp_catalog.cpp"
        "${ROOT}/include/tetr/block.h"
        "${ROOT}/include/tetr/legacy/bot_environment.h"
        "${ROOT}/src/legacy/block_compat.cpp"
        "${ROOT}/src/bot/bot_compat_environment.h")
    if(EXISTS "${REMOVED_FILE}")
        message(FATAL_ERROR "removed BOT compatibility artifact returned: ${REMOVED_FILE}")
    endif()
endforeach()
file(GLOB_RECURSE ENGINE_AND_RULES
    "${ROOT}/src/engine/*.cpp"
    "${ROOT}/src/rules/*.cpp")
foreach(FILE IN LISTS ENGINE_AND_RULES)
    assert_not_contains("${FILE}" "Piece::Type::from_legacy(103" "April-Fools pieces must use catalog helpers")
endforeach()

# Board-cell legacy integers are format-boundary only, not render/engine color API.
# Check specifically for legacy conversion in renderer arguments.
foreach(FILE IN LISTS ENGINE_FILES)
    assert_not_contains("${FILE}" "set_board_cell_color(cell.legacy_value()" "renderer must consume BoardCell directly")
    assert_not_contains("${FILE}" "set_board_cell_color(value.legacy_value()" "renderer must consume BoardCell directly")
endforeach()

# Terminal lifecycle is RAII-only; the public API exposes only semantic text-input mode.
foreach(SYMBOL initialize_terminal restore_terminal hide_cursor show_cursor)
    assert_not_contains("${ROOT}/include/tetr/render/terminal_renderer.h" "${SYMBOL}(" "terminal lifecycle must be owned by TerminalSession")
endforeach()

# Derived key bindings were removed from persistent UserConfig state.
assert_not_contains("${ROOT}/include/tetr/config/user_config.h" "std::map<int,std::string> bindings" "key bindings are derived Input state, not persisted duplicate state")
assert_not_contains("${ROOT}/include/tetr/config/user_config.h" "std::map<int, std::string> bindings" "key bindings are derived Input state, not persisted duplicate state")


# Stage 104: historical integer/seconds encodings live only at explicit
# compatibility boundaries. Runtime/Rules/Engine/Render must operate solely on
# semantic domain types and may not include the compatibility codec.
set(TYPED_CORE_FILES)
foreach(PATTERN
        "${ROOT}/include/tetr/runtime/*.h"
        "${ROOT}/src/runtime/*.cpp"
        "${ROOT}/src/runtime/*.h"
        "${ROOT}/include/tetr/rules/*.h"
        "${ROOT}/src/rules/*.cpp"
        "${ROOT}/include/tetr/engine/*.h"
        "${ROOT}/src/engine/*.cpp"
        "${ROOT}/src/engine/*.h"
        "${ROOT}/include/tetr/render/*.h"
        "${ROOT}/src/render/*.cpp"
        "${ROOT}/src/render/*.h")
    file(GLOB MATCHED ${PATTERN})
    list(APPEND TYPED_CORE_FILES ${MATCHED})
endforeach()
foreach(FILE IN LISTS TYPED_CORE_FILES)
    assert_not_contains("${FILE}" "#include \"tetr/compat/" "typed core must not depend on historical encoding codecs")
endforeach()

# Domain types themselves must not grow protocol-format methods again.
foreach(FILE
        "${ROOT}/include/tetr/runtime/board_cell.h"
        "${ROOT}/include/tetr/runtime/gravity.h"
        "${ROOT}/include/tetr/runtime/garbage_hole_pattern.h"
        "${ROOT}/include/tetr/runtime/strong_types.h"
        "${ROOT}/include/tetr/runtime/game_modes.h")
    foreach(SYMBOL "from_legacy(" "legacy_value(" "legacy_seconds(" ".legacy(")
        assert_not_contains("${FILE}" "${SYMBOL}" "domain types must not expose historical wire/BOT encodings")
    endforeach()
endforeach()


# Stage 105: BOT implementations are normal .cpp translation units. Public BOT
# headers contain declarations only, and retired implementation variants must
# not return under their historical filenames/namespaces.
foreach(RETIRED
        "${ROOT}/include/tetr/bot/bot_while_1.h"
        "${ROOT}/include/tetr/bot/bot_while2-.h")
    if(EXISTS "${RETIRED}")
        message(FATAL_ERROR "retired BOT header still exists: ${RETIRED}")
    endif()
endforeach()
foreach(FILE
        "${ROOT}/include/tetr/bot/bot_while1.h"
        "${ROOT}/include/tetr/bot/bot_candy0014.h"
        "${ROOT}/include/tetr/bot/bot_mech.h"
        "${ROOT}/include/tetr/bot/bot_zzztoj.h")
    assert_not_contains("${FILE}" "bool init(){" "BOT headers must contain declarations only")
    assert_not_contains("${FILE}" "bool init() {" "BOT headers must contain declarations only")
endforeach()
foreach(FILE
        "${ROOT}/src/bot/bot_while1.cpp"
        "${ROOT}/src/bot/bot_candy0014.cpp"
        "${ROOT}/src/bot/bot_mech.cpp"
        "${ROOT}/src/bot/bot_zzztoj.cpp")
    if(NOT EXISTS "${FILE}")
        message(FATAL_ERROR "BOT implementation source missing: ${FILE}")
    endif()
endforeach()
# Stage 107: BOT configuration/time is an explicit call snapshot; the old
# process-global Config/BoardStatic/timer environment and Block wrapper must not return.
foreach(FILE
        "${ROOT}/src/bot/bot_while1.cpp"
        "${ROOT}/src/bot/bot_candy0014.cpp"
        "${ROOT}/src/bot/bot_mech.cpp"
        "${ROOT}/src/bot/bot_zzztoj.cpp"
        "${ROOT}/src/legacy/bot.cpp")
    assert_not_contains("${FILE}" "Config::" "BOT code must use explicit Bot::Environment")
    assert_not_contains("${FILE}" "BoardStatic::" "BOT code must use explicit Bot::Environment")
    assert_not_contains("${FILE}" "LegacyTimerProxy" "legacy BOT timer proxy must not return")
    assert_not_contains("${FILE}" "Block::block" "BOT geometry must use Piece::Definition")
    assert_not_contains("${FILE}" "tetr/block.h" "removed Block compatibility header must not return")
endforeach()
assert_not_contains("${ROOT}/src/bot/bot_while1.cpp" "playerp" "bot_while1 must not bind player state through a global pointer")
assert_not_contains("${ROOT}/include/tetr/runtime/monotonic_clock.h" "double get()" "MonotonicClock legacy scalar time API must not return")
assert_not_contains("${ROOT}/include/tetr/runtime/monotonic_clock.h" "void sleep(double" "MonotonicClock legacy scalar sleep API must not return")
assert_not_contains("${ROOT}/src/legacy/bot.cpp" "Bot_while2::" "retired bot_while2 namespace must not be dispatched")
assert_not_contains("${ROOT}/src/legacy/bot.cpp" "Bot_mech_all::" "retired bot_mech_all namespace must not be dispatched")
assert_not_contains("${ROOT}/src/app/settings_model.cpp" "integer(SettingId::AutoplayMode" "BOT selection must be a named choice")


# Stage 108: configuration metadata has one schema source of truth. Disk parsing
# and Settings UI must not grow their historical parallel registration tables again.
assert_not_contains("${ROOT}/src/config/user_config_file.cpp" "Racing Distance" "user config parser must consume settings_schema")
assert_not_contains("${ROOT}/src/config/user_config_file.cpp" "Garbage Model" "user config parser must consume settings_schema")
assert_not_contains("${ROOT}/src/app/settings_model.cpp" "Racing Distance" "settings UI must consume settings_schema")
assert_not_contains("${ROOT}/src/app/settings_model.cpp" "Rotation System" "settings UI must consume settings_schema")
assert_not_contains("${ROOT}/src/replay/config_codec.cpp" "if (name == \"Model\")" "Replay config fields must remain table-driven")
foreach(FILE
        "${ROOT}/src/network/board_state_codec.cpp"
        "${ROOT}/src/network/lobby_protocol.cpp"
        "${ROOT}/src/network/frame_packet.cpp")
    assert_not_contains("${FILE}" "std::from_chars" "network text parsing must use Util::Text codec")
endforeach()
assert_not_contains("${ROOT}/src/rules/garbage_generation.cpp" "deterministic_shuffle_holes" "deterministic shuffle must have a single implementation")


# Stage 109: historical permissive text parsing is an explicit Config/Replay
# compatibility boundary rather than a generic Util or App facility.
assert_not_contains("${ROOT}/include/tetr/util/numeric.h" "parse_int_loose" "generic numeric util must not expose permissive legacy parsing")
assert_not_contains("${ROOT}/include/tetr/util/numeric.h" "parse_double_loose" "generic numeric util must not expose permissive legacy parsing")
foreach(FILE
        "${ROOT}/src/app/settings_menu.cpp"
        "${ROOT}/src/app/settings_model.cpp")
    assert_not_contains("${FILE}" "tetr/compat/legacy_text.h" "Settings app must delegate historical scalar parsing to tetr_config")
endforeach()
assert_not_contains("${ROOT}/include/tetr/input/action.h" "parse_action(" "retired string action parser must not return")
assert_not_contains("${ROOT}/include/tetr/config/user_config.h" "control_names(" "retired keybinding short-name table must not return")
assert_not_contains("${ROOT}/include/tetr/util/deterministic_random.h" "legacy_shuffle" "deterministic RNG API must use semantic naming")
if(EXISTS "${ROOT}/include/tetr/util/random.h" OR EXISTS "${ROOT}/src/util/random.cpp")
    message(FATAL_ERROR "retired generic random compatibility files returned")
endif()
assert_not_contains("${ROOT}/src/replay/operation_codec.cpp" "std::vector<std::string> split_record_fields" "known Replay operations must parse through string views")
assert_not_contains("${ROOT}/src/runtime/piece_catalog_data.h" "extern const" "Piece Catalog source data must be function-local lazy storage")
assert_not_contains("${ROOT}/src/network/frame_packet.cpp" "text.find(';')" "fixed-width attack packet parsing must use split_exact")


# Stage 110: Engine is layered. Core owns simulation algorithms only and must
# never grow terminal/input/Replay/Network dependencies again. Presentation may
# render and inspect input, but transport/Replay side effects belong to the
# integration/gameplay layers.
set(ENGINE_CORE_SOURCES
    field_mutation.cpp
    garbage_field.cpp
    board_lifecycle.cpp
    piece_queue.cpp
    placement_application.cpp
    movement_controller.cpp
    periodic_events.cpp
    frame_board_adapter.cpp
    placement_board_adapter.cpp
    locked_piece_resolution.cpp)
foreach(NAME IN LISTS ENGINE_CORE_SOURCES)
    set(FILE "${ROOT}/src/engine/${NAME}")
    foreach(LAYER render network replay input)
        assert_not_contains("${FILE}" "#include \"tetr/${LAYER}/" "engine core must remain IO/presentation independent")
    endforeach()
endforeach()
set(ENGINE_PRESENTATION_SOURCES
    movement_presentation.cpp
    line_clear_presentation.cpp
    garbage_rendering.cpp
    next_preview_presentation.cpp
    placement_presentation.cpp
    countdown_presentation.cpp
    game_end_presentation.cpp)
foreach(NAME IN LISTS ENGINE_PRESENTATION_SOURCES)
    set(FILE "${ROOT}/src/engine/${NAME}")
    foreach(LAYER network replay)
        assert_not_contains("${FILE}" "#include \"tetr/${LAYER}/" "engine presentation must not own transport/Replay side effects")
    endforeach()
endforeach()
assert_not_contains("${ROOT}/src/engine/piece_queue.cpp" "tetr/render/" "piece queue generation must not render NEXT preview")
assert_not_contains("${ROOT}/CMakeLists.txt" "add_library(tetr_engine STATIC" "monolithic tetr_engine target must not return")


# Stage 111: blocking/timed wait primitives replace hot polling in long-lived UI,
# lobby and replay waits. Keep the most expensive historical spins from returning.
assert_not_contains("${ROOT}/src/engine/countdown_presentation.cpp" "Runtime::GarbageMode::Multiplayer) continue;" "multiplayer countdown must sleep to its deadline")
assert_not_contains("${ROOT}/src/network/lobby_channel.cpp" "if (auto wire = try_receive(network_services(context)))" "lobby waits must use blocking/timed receive")
assert_not_contains("${ROOT}/src/app/init_multiplayer.cpp" "Network::try_receive_lobby_signal(context)" "interactive lobby must use bounded blocking receive")
assert_not_contains("${ROOT}/src/engine/autoplay_adapter.cpp" "if (!playback.commandDue(context.now())) continue;" "autoplay presentation must yield between command deadlines")

# Stage 112: text-input mode is owned by the explicitly attached TerminalSession.
# Signal emergency restoration may keep process-global POD snapshots, but no live
# TerminalSessionState pointer is allowed outside the owning object.
assert_not_contains("${ROOT}/src/render/terminal_renderer.cpp" "TerminalSessionState* activeSession" "terminal lifecycle must not use a mutable global session pointer")
assert_not_contains("${ROOT}/include/tetr/render/terminal_renderer.h" "\nvoid begin_text_input();" "text input mode must be a TerminalSession method")
assert_not_contains("${ROOT}/include/tetr/render/terminal_renderer.h" "\nvoid end_text_input();" "text input mode must be a TerminalSession method")

# Stage 113: compatibility encoding is split by domain. The umbrella remains for
# external/test compatibility only; production code must use narrow codecs.
file(GLOB_RECURSE PRODUCTION_CPP "${ROOT}/src/*.cpp")
foreach(FILE IN LISTS PRODUCTION_CPP)
    assert_not_contains("${FILE}" "#include \\\"tetr/compat/legacy_encoding.h\\\"" "production code must include a narrow compatibility codec")
endforeach()

message(STATUS "Architecture boundaries verified")
