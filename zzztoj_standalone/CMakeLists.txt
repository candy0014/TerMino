cmake_minimum_required(VERSION 3.16)
project(zzztoj_standalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
    if(MSVC)
        add_link_options(/STACK:16777216)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_link_options(-Wl,--stack,16777216)
    endif()
endif()
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Threads REQUIRED)

# Upstream search/scoring formulas are retained with bounds-only safety fixes;
# rule_srs.cpp is additionally
# compiled as the shape/90-degree-kick base for the local tetr-SRS+
# reachability wrapper. The main game compiles this same checkout under upstream/.
add_library(zzztoj_native STATIC
    upstream/src/ai_zzz.cpp
    upstream/src/integer_utils.cpp
    upstream/src/random.cpp
    upstream/src/rule_botris.cpp
    upstream/src/rule_srs.cpp
    upstream/src/search_aspin.cpp
    upstream/src/search_cautious.cpp
    upstream/src/search_path.cpp
    upstream/src/search_simple.cpp
    upstream/src/search_simulate.cpp
    upstream/src/search_tspin.cpp
    upstream/src/tetris_core.cpp
)
target_include_directories(zzztoj_native SYSTEM PUBLIC upstream/src)
target_link_libraries(zzztoj_native PUBLIC Threads::Threads)

add_executable(zzztoj_visualizer
    src/bot_adapter.cpp
    src/main.cpp
    src/rule_tetr_srs_plus.cpp
    src/tetr_attack.cpp
    src/tetr_spin.cpp
)
target_link_libraries(zzztoj_visualizer PRIVATE zzztoj_native Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zzztoj_visualizer PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(tetr_attack_test tests/tetr_attack_test.cpp src/tetr_attack.cpp)
    target_include_directories(tetr_attack_test PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(tetr_attack_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME tetrio_attack_table COMMAND tetr_attack_test)

    add_executable(tetrio_attack_spec_test
        tests/tetrio_attack_spec_test.cpp
        src/tetr_attack.cpp
    )
    target_include_directories(tetrio_attack_spec_test PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(tetrio_attack_spec_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME tetrio_attack_spec COMMAND tetrio_attack_spec_test)

    add_executable(tetrio_spin_test
        tests/tetrio_spin_test.cpp
        src/tetr_spin.cpp
    )
    target_include_directories(tetrio_spin_test PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(tetrio_spin_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME tetrio_all_mini_plus COMMAND tetrio_spin_test)

    # Test-only oracle for SRS+: when this folder is inside the tetr checkout,
    # compare the local reachability wrapper against tetr's kick catalog.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../src/runtime/piece_catalog_data.cpp")
        add_executable(srs_plus_rule_test
            tests/srs_plus_rule_test.cpp
            src/rule_tetr_srs_plus.cpp
            ../src/runtime/piece_catalog_data.cpp
        )
        target_include_directories(srs_plus_rule_test PRIVATE
            src
            ../src/runtime
            ../include
        )
        target_link_libraries(srs_plus_rule_test PRIVATE zzztoj_native)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(srs_plus_rule_test PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME tetr_srs_plus_rule_parity COMMAND srs_plus_rule_test)
    endif()

    foreach(bot_name IN ITEMS botris toj dig qq c2)
        add_test(NAME "tetris_ai_runner_${bot_name}_smoke"
            COMMAND zzztoj_visualizer --headless --bot "${bot_name}"
                                       --pieces 20 --strength 1 --search-ms 2
                                       --seed 20260919)
        set_tests_properties("tetris_ai_runner_${bot_name}_smoke"
            PROPERTIES TIMEOUT 20)
    endforeach()

    foreach(test_target IN ITEMS tetr_attack_test tetrio_attack_spec_test tetrio_spin_test srs_plus_rule_test)
        if(TARGET ${test_target})
            if(MSVC)
                target_compile_options(${test_target} PRIVATE /UNDEBUG)
            else()
                target_compile_options(${test_target} PRIVATE -UNDEBUG)
            endif()
        endif()
    endforeach()
endif()
