option(FF3_BUILD_GENERATED "Compile locally generated FF3 code and the diagnostic host" OFF)
if(FF3_BUILD_GENERATED)
set(FF3_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/out/media/install" CACHE PATH "Minimal shared FFmpeg install")
find_path(FF3_FFMPEG_INCLUDE libavcodec/avcodec.h HINTS "${FF3_FFMPEG_ROOT}/include" REQUIRED)
foreach(component avcodec avformat avutil swresample swscale)
    find_library(FF3_${component}_LIBRARY NAMES ${component} HINTS "${FF3_FFMPEG_ROOT}/lib" REQUIRED)
    list(APPEND FF3_FFMPEG_LIBRARIES "${FF3_${component}_LIBRARY}")
endforeach()
set(FF3_SHARED_RENDERER "${CMAKE_CURRENT_SOURCE_DIR}/../vcs/host")
add_library(ff3_software_renderer STATIC
    "${FF3_SHARED_RENDERER}/ge_renderer.cpp"
    "${FF3_SHARED_RENDERER}/framebuffer_capture.cpp"
    src/software_backend.cpp src/display_host.cpp src/file_host.cpp src/sas_host.cpp src/audio_host.cpp src/atrac_host.cpp src/utility_host.cpp src/savedata_host.cpp src/movie_host.cpp "${FF3_SHARED_RENDERER}/vcs_media_decoder.cpp")
target_link_libraries(ff3_software_renderer PUBLIC psprecomp_core ff3_bootstrap_hle ${FF3_FFMPEG_LIBRARIES})
target_include_directories(ff3_software_renderer PRIVATE "${FF3_FFMPEG_INCLUDE}" PUBLIC "${FF3_SHARED_RENDERER}" "${CMAKE_CURRENT_SOURCE_DIR}/src")

endif()

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_library(ff3_bootstrap_hle STATIC src/bootstrap_hle.cpp src/bootstrap_memory.cpp src/guest_printf.cpp src/bootstrap_pools.cpp src/bootstrap_callbacks.cpp src/bootstrap_sync.cpp)
target_link_libraries(ff3_bootstrap_hle PUBLIC psprecomp_core)
target_include_directories(ff3_bootstrap_hle PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(FF3_PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(FF3_ELF "${FF3_PROFILE_DIR}/game/EBOOT.ELF" CACHE FILEPATH
    "Locally prepared, decrypted FF3 ELF/PRX")
set(FF3_CONFIG "${FF3_PROFILE_DIR}/config/profile.json" CACHE FILEPATH
    "FF3 executable identity and generation settings")
set(FF3_ANALYSIS_DIR "${FF3_PROFILE_DIR}/analysis" CACHE PATH
    "Local FF3 analysis and generated output")

foreach(stage analyze generate)
    add_custom_target(ff3_${stage}
        COMMAND "${Python3_EXECUTABLE}" "${FF3_PROFILE_DIR}/tools/ff3.py"
            ${stage} "${FF3_ELF}" --config "${FF3_CONFIG}"
            --analysis-dir "${FF3_ANALYSIS_DIR}"
            --analyzer "$<TARGET_FILE:psp_analyze>"
            --recompiler "$<TARGET_FILE:psp_recomp>"
        DEPENDS psp_analyze psp_recomp
        USES_TERMINAL VERBATIM)
endforeach()

# Configure after generation: this target checks translation, not guest boot.
option(FF3_BUILD_GENERATED "Compile locally generated FF3 code and the diagnostic host" OFF)
if(FF3_BUILD_GENERATED)
    set(FF3_GENERATED_DIR "${FF3_ANALYSIS_DIR}/generated")
    if(NOT EXISTS "${FF3_GENERATED_DIR}/ff3_codegen.json" OR
       NOT EXISTS "${FF3_GENERATED_DIR}/generated_registry.cpp")
        message(FATAL_ERROR "Run ff3_generate with FF3_BUILD_GENERATED=OFF, then reconfigure with it ON")
    endif()
    file(GLOB FF3_GENERATED_UNITS CONFIGURE_DEPENDS
        "${FF3_GENERATED_DIR}/generated_unit_*.cpp")
    if(NOT FF3_GENERATED_UNITS)
        message(FATAL_ERROR "FF3 generated corpus contains no translation units")
    endif()
    add_library(ff3_generated STATIC
        "${FF3_GENERATED_DIR}/generated_registry.cpp" ${FF3_GENERATED_UNITS})
    target_link_libraries(ff3_generated PUBLIC psprecomp_core)
    target_include_directories(ff3_generated PRIVATE "${FF3_GENERATED_DIR}")
    if(MSVC)
        target_compile_options(ff3_generated PRIVATE /bigobj)
    else()
        target_compile_options(ff3_generated PRIVATE "-O${PSPRECOMP_GENERATED_OPT_LEVEL}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${FF3_GENERATED_DIR}/ff3_codegen.json" "${FF3_ELF}")
    file(READ "${FF3_GENERATED_DIR}/ff3_codegen.json" FF3_CORPUS_METADATA)
    string(JSON FF3_CORPUS_SHA256 GET "${FF3_CORPUS_METADATA}" sha256)
    string(JSON FF3_CORPUS_BASE GET "${FF3_CORPUS_METADATA}" profile load_base)
    file(SHA256 "${FF3_ELF}" FF3_CURRENT_SHA256)
    if(NOT FF3_CURRENT_SHA256 STREQUAL FF3_CORPUS_SHA256)
        message(FATAL_ERROR "FF3_ELF differs from generated corpus; regenerate before compiling")
    endif()
    configure_file(src/identity.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/ff3_identity.hpp" @ONLY)
    add_executable(ff3_probe src/main.cpp)
    target_link_libraries(ff3_probe PRIVATE ff3_generated ff3_software_renderer)
    option(FF3_BUILD_WINDOW "Build the macOS interactive presentation window" ${APPLE})
    if(FF3_BUILD_WINDOW)
        if(NOT APPLE)
            message(FATAL_ERROR "FF3_BUILD_WINDOW currently requires macOS")
        endif()
        enable_language(OBJCXX)
        add_library(ff3_native_window STATIC src/native_window.mm src/native_audio.cpp)
        set_target_properties(ff3_native_window PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(ff3_native_window PRIVATE -fobjc-arc)
        target_link_libraries(ff3_native_window PUBLIC ff3_bootstrap_hle "-framework AppKit" "-framework AudioToolbox")
        target_link_libraries(ff3_probe PRIVATE ff3_native_window)
        target_compile_definitions(ff3_probe PRIVATE FF3_NATIVE_WINDOW=1)
        add_executable(ff3_player MACOSX_BUNDLE src/main.cpp)
        target_link_libraries(ff3_player PRIVATE ff3_generated ff3_software_renderer ff3_native_window)
        target_include_directories(ff3_player PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_definitions(ff3_player PRIVATE FF3_NATIVE_WINDOW=1
            FF3_PLAYER_ELF="${FF3_ELF}")
        set_target_properties(ff3_player PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.psprecomp.ff3"
            MACOSX_BUNDLE_BUNDLE_NAME "FF3 Native"
            MACOSX_BUNDLE_BUNDLE_VERSION "0.1")
    endif()
    option(FF3_BUILD_FONT "Compile the locally prepared FF3 font module" OFF)
    if(FF3_BUILD_FONT)
        set(FF3_FONT_DIR "${FF3_ANALYSIS_DIR}/font_generated")
        if(NOT EXISTS "${FF3_FONT_DIR}/font_identity.hpp")
            message(FATAL_ERROR "Run tools/prepare_font.py before enabling FF3_BUILD_FONT")
        endif()
        file(GLOB FF3_FONT_UNITS "${FF3_FONT_DIR}/generated_unit_*.cpp")
        add_library(ff3_font STATIC "${FF3_FONT_DIR}/generated_registry.cpp" ${FF3_FONT_UNITS} src/font_module.cpp)
        target_link_libraries(ff3_font PUBLIC ff3_bootstrap_hle)
        target_include_directories(ff3_font PRIVATE "${FF3_FONT_DIR}")
        target_link_libraries(ff3_probe PRIVATE ff3_font)
        target_compile_definitions(ff3_probe PRIVATE FF3_NATIVE_FONT=1)
        if(TARGET ff3_player)
            target_link_libraries(ff3_player PRIVATE ff3_font)
            target_compile_definitions(ff3_player PRIVATE FF3_NATIVE_FONT=1)
        endif()
    endif()
    target_include_directories(ff3_probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

if(PSPRECOMP_BUILD_PROFILE_TESTS)
    enable_testing()
    add_executable(ff3_bootstrap_tests tests/bootstrap_execution.cpp)
    target_link_libraries(ff3_bootstrap_tests PRIVATE ff3_bootstrap_hle)
    add_test(NAME ff3_bootstrap_tests COMMAND ff3_bootstrap_tests)
    add_test(NAME ff3_tooling_tests
        COMMAND "${Python3_EXECUTABLE}" "${FF3_PROFILE_DIR}/tests/test_tooling.py"
            --analyzer "$<TARGET_FILE:psp_analyze>"
            --recompiler "$<TARGET_FILE:psp_recomp>")
    add_test(NAME ff3_setup_tests
        COMMAND "${Python3_EXECUTABLE}" "${FF3_PROFILE_DIR}/tests/test_setup.py")
endif()

if(PSPRECOMP_BUILD_PROFILE_TESTS)
    set(FF3_INSTRUCTION_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/instruction_fixture")
    add_custom_command(
        OUTPUT "${FF3_INSTRUCTION_FIXTURE}/generated_registry.cpp"
               "${FF3_INSTRUCTION_FIXTURE}/generated_unit_0000.cpp"
        COMMAND "${Python3_EXECUTABLE}" "${FF3_PROFILE_DIR}/tests/instruction_fixture.py"
                "$<TARGET_FILE:psp_recomp>" "${FF3_INSTRUCTION_FIXTURE}"
        COMMAND "${CMAKE_COMMAND}" -E touch
                "${FF3_INSTRUCTION_FIXTURE}/generated_registry.cpp"
                "${FF3_INSTRUCTION_FIXTURE}/generated_unit_0000.cpp"
        DEPENDS psp_recomp "${FF3_PROFILE_DIR}/tests/instruction_fixture.py"
        VERBATIM)
    add_executable(ff3_instruction_tests tests/instruction_execution.cpp
        "${FF3_INSTRUCTION_FIXTURE}/generated_registry.cpp"
        "${FF3_INSTRUCTION_FIXTURE}/generated_unit_0000.cpp")
    target_link_libraries(ff3_instruction_tests PRIVATE psprecomp_core)
    add_test(NAME ff3_instruction_tests COMMAND ff3_instruction_tests)
endif()

if(PSPRECOMP_BUILD_PROFILE_TESTS)
    set(FF3_DENSE_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/dense_instruction_fixture")
    set(FF3_DENSE_SOURCES "${FF3_DENSE_FIXTURE}/generated_registry.cpp"
        "${FF3_DENSE_FIXTURE}/generated_unit_0000.cpp"
        "${FF3_DENSE_FIXTURE}/generated_unit_0002.cpp")
    add_custom_command(OUTPUT ${FF3_DENSE_SOURCES}
        COMMAND "${Python3_EXECUTABLE}" "${FF3_PROFILE_DIR}/tests/instruction_fixture.py"
                "$<TARGET_FILE:psp_recomp>" "${FF3_DENSE_FIXTURE}" 0x4000
        COMMAND "${CMAKE_COMMAND}" -E touch ${FF3_DENSE_SOURCES}
        DEPENDS psp_recomp "${FF3_PROFILE_DIR}/tests/instruction_fixture.py"
        VERBATIM)
    add_executable(ff3_instruction_dense_tests tests/instruction_execution.cpp ${FF3_DENSE_SOURCES})
    target_link_libraries(ff3_instruction_dense_tests PRIVATE psprecomp_core)
    add_test(NAME ff3_instruction_dense_tests COMMAND ff3_instruction_dense_tests)
endif()

if(PSPRECOMP_BUILD_PROFILE_TESTS AND FF3_BUILD_GENERATED)
    add_executable(ff3_movie_check EXCLUDE_FROM_ALL tests/movie_playback.cpp)
    target_link_libraries(ff3_movie_check PRIVATE ff3_software_renderer)
    add_executable(ff3_host_tests tests/host_execution.cpp)
    target_link_libraries(ff3_host_tests PRIVATE ff3_software_renderer)
    add_test(NAME ff3_host_tests COMMAND ff3_host_tests)
endif()
