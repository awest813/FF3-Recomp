# FF3 profile.
#
# The host library and its regression tests build on Linux, Windows, and macOS
# without game files. The headless probe (ff3_probe) additionally needs a local
# generated corpus and FFmpeg. The interactive window remains the macOS player.

option(FF3_BUILD_GENERATED "Compile locally generated FF3 code and the headless diagnostic host" OFF)
option(FF3_BUILD_FONT "Compile the locally prepared FF3 font module" OFF)
option(FF3_BUILD_WINDOW "Build the macOS interactive presentation window" OFF)

if(FF3_BUILD_WINDOW AND NOT APPLE)
    message(FATAL_ERROR
        "FF3_BUILD_WINDOW builds the AppKit player and requires macOS. "
        "Windows and Linux build the headless ff3_probe with FF3_BUILD_WINDOW=OFF. "
        "See profiles/ff3/WINDOWS_LINUX.md")
endif()
if(FF3_BUILD_WINDOW AND NOT FF3_BUILD_GENERATED)
    message(FATAL_ERROR "FF3_BUILD_WINDOW requires FF3_BUILD_GENERATED=ON")
endif()

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

add_library(ff3_bootstrap_hle STATIC
    src/bootstrap_hle.cpp src/bootstrap_memory.cpp src/guest_printf.cpp
    src/bootstrap_pools.cpp src/bootstrap_callbacks.cpp src/bootstrap_sync.cpp)
target_link_libraries(ff3_bootstrap_hle PUBLIC psprecomp_core)
target_include_directories(ff3_bootstrap_hle PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_definitions(ff3_bootstrap_hle PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(FF3_PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(FF3_ELF "${FF3_PROFILE_DIR}/game/EBOOT.ELF" CACHE FILEPATH
    "Locally prepared, decrypted FF3 ELF/PRX")
set(FF3_CONFIG "${FF3_PROFILE_DIR}/config/profile.json" CACHE FILEPATH
    "FF3 executable identity and generation settings")
set(FF3_ANALYSIS_DIR "${FF3_PROFILE_DIR}/analysis" CACHE PATH
    "Local FF3 analysis and generated output")
set(FF3_SHARED_RENDERER "${CMAKE_CURRENT_SOURCE_DIR}/../vcs/host")

# Software GE host used by the probe and by asset-free regression tests.
# Media decoding stays out of this library so CI does not need FFmpeg.
add_library(ff3_host STATIC
    "${FF3_SHARED_RENDERER}/ge_renderer.cpp"
    "${FF3_SHARED_RENDERER}/framebuffer_capture.cpp"
    src/software_backend.cpp
    src/display_host.cpp
    src/file_host.cpp
    src/sas_host.cpp
    src/audio_host.cpp
    src/utility_host.cpp
    src/savedata_host.cpp)
target_link_libraries(ff3_host PUBLIC psprecomp_core ff3_bootstrap_hle Threads::Threads)
target_include_directories(ff3_host PUBLIC
    "${FF3_SHARED_RENDERER}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_definitions(ff3_host PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ff3_host PRIVATE /bigobj /EHsc ${PSPRECOMP_MSVC_MP_FLAG})
endif()

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
if(FF3_BUILD_GENERATED)
    set(FF3_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/out/media/install" CACHE PATH "Minimal shared FFmpeg install")
    if(NOT EXISTS "${FF3_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR
            "FF3_BUILD_GENERATED needs FFmpeg 7 headers at ${FF3_FFMPEG_ROOT}/include. "
            "Build them with profiles/ff3/tools/build_media.py or set FF3_FFMPEG_ROOT. "
            "See profiles/ff3/WINDOWS_LINUX.md")
    endif()
    find_path(FF3_FFMPEG_INCLUDE libavcodec/avcodec.h
        HINTS "${FF3_FFMPEG_ROOT}/include" REQUIRED)
    set(FF3_FFMPEG_LIBRARIES)
    foreach(component avcodec avformat avutil swresample swscale)
        find_library(FF3_${component}_LIBRARY NAMES ${component}
            HINTS "${FF3_FFMPEG_ROOT}/lib" "${FF3_FFMPEG_ROOT}/bin" REQUIRED)
        if(MSVC AND FF3_${component}_LIBRARY MATCHES "\\.dll\\.a$")
            message(FATAL_ERROR
                "FF3_FFMPEG_ROOT contains MinGW import libraries, which MSVC cannot link. "
                "Point FF3_FFMPEG_ROOT at an MSVC FFmpeg build, or configure with the compiler that built FFmpeg. "
                "See profiles/ff3/WINDOWS_LINUX.md")
        endif()
        list(APPEND FF3_FFMPEG_LIBRARIES "${FF3_${component}_LIBRARY}")
    endforeach()

    add_library(ff3_media STATIC
        "${FF3_SHARED_RENDERER}/vcs_media_decoder.cpp"
        src/atrac_host.cpp
        src/movie_host.cpp)
    target_link_libraries(ff3_media PUBLIC ff3_host ${FF3_FFMPEG_LIBRARIES})
    target_include_directories(ff3_media PRIVATE "${FF3_FFMPEG_INCLUDE}")

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
        target_compile_options(ff3_generated PRIVATE /bigobj /EHsc ${PSPRECOMP_MSVC_MP_FLAG})
        target_compile_definitions(ff3_generated PRIVATE _CRT_SECURE_NO_WARNINGS)
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

    function(ff3_finish_generated_host name)
        target_link_libraries(${name} PRIVATE ff3_generated ff3_media)
        target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        if(UNIX AND NOT APPLE)
            set_property(TARGET ${name} APPEND PROPERTY BUILD_RPATH "${FF3_FFMPEG_ROOT}/lib")
        endif()
        if(WIN32 AND EXISTS "${FF3_FFMPEG_ROOT}/bin")
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${FF3_FFMPEG_ROOT}/bin" "$<TARGET_FILE_DIR:${name}>")
        endif()
        if(MSVC)
            target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    endfunction()

    add_executable(ff3_probe src/main.cpp)
    ff3_finish_generated_host(ff3_probe)
    if(FF3_BUILD_WINDOW)
        enable_language(OBJCXX)
        add_library(ff3_native_window STATIC src/native_window.mm src/native_audio.cpp)
        set_target_properties(ff3_native_window PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(ff3_native_window PRIVATE -fobjc-arc)
        target_link_libraries(ff3_native_window PUBLIC ff3_bootstrap_hle "-framework AppKit" "-framework AudioToolbox")
        target_link_libraries(ff3_probe PRIVATE ff3_native_window)
        target_compile_definitions(ff3_probe PRIVATE FF3_NATIVE_WINDOW=1)
        add_executable(ff3_player MACOSX_BUNDLE src/main.cpp)
        ff3_finish_generated_host(ff3_player)
        target_link_libraries(ff3_player PRIVATE ff3_native_window)
        target_compile_definitions(ff3_player PRIVATE FF3_NATIVE_WINDOW=1
            FF3_PLAYER_ELF="${FF3_ELF}")
        set_target_properties(ff3_player PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.psprecomp.ff3"
            MACOSX_BUNDLE_BUNDLE_NAME "FF3 Native"
            MACOSX_BUNDLE_BUNDLE_VERSION "0.1")
    endif()
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
    if(PSPRECOMP_BUILD_PROFILE_TESTS)
        add_executable(ff3_movie_check EXCLUDE_FROM_ALL tests/movie_playback.cpp)
        ff3_finish_generated_host(ff3_movie_check)
    endif()
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
    add_executable(ff3_host_tests tests/host_execution.cpp)
    target_link_libraries(ff3_host_tests PRIVATE ff3_host)
    if(MSVC)
        target_compile_definitions(ff3_host_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME ff3_host_tests COMMAND ff3_host_tests)
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
