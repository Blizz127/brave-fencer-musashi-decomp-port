cmake_minimum_required(VERSION 3.16)

# CXX is required by PsyCross, whose renderer and GTE are C++.
project(brave_fencer_musashi_decomp LANGUAGES C CXX)

option(MUSASHI_BUILD_PC_PORT "Build the platform-neutral PC port smoke target" ON)
option(MUSASHI_WITH_PSYCROSS "Link the PC port against the PsyCross Psy-Q reimplementation" ON)
# Native lane (docs/PC-PORT.md, "Native lane"): compile admitted byte-matched
# decomp C for the host and run it in place of the interpreted words. Needs
# clang; off until the live native boot has been checked with it on.
option(MUSASHI_NATIVE_LANE "Run checker-admitted decomp C natively in the PC port" OFF)
option(MUSASHI_USE_VENDOR_BFM
    "Prefer cleaned house-style matched leaves reconciled with vendor/bfm-decomp (Druthulu)"
    OFF)

# PsyCross (MIT) reimplements libgpu, libgte, libspu, libcd, libetc, libapi and
# libpad for PC. It is vendored at a pinned commit by tools/fetch_toolchains.sh
# and its checkout is ignored.
#
# Its own CMakeLists.txt is deliberately not used. It calls find_package(OPENAL)
# in upper case, which only resolves on case-insensitive filesystems, and links
# a bare `OpenAL` target that modern config packages do not export. Owning the
# integration here avoids carrying a patch against an ignored checkout: the
# upstream build file is only a source glob and two find_package calls.
if(MUSASHI_BUILD_PC_PORT AND MUSASHI_WITH_PSYCROSS)
    set(MUSASHI_PSYCROSS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/third_party/psycross")
    if(NOT EXISTS "${MUSASHI_PSYCROSS_DIR}/include/PsyX/PsyX_public.h")
        message(FATAL_ERROR
            "PsyCross not found under tools/third_party/psycross; run "
            "./tools/fetch_toolchains.sh, or configure with -DMUSASHI_WITH_PSYCROSS=OFF")
    endif()

    if(EXISTS "/home/linuxbrew/.linuxbrew/opt/sdl2/include/SDL2/SDL_main.h")
        include_directories(BEFORE SYSTEM "/home/linuxbrew/.linuxbrew/opt/sdl2/include/SDL2" "/home/linuxbrew/.linuxbrew/opt/sdl2/include")
    endif()
    # Optional private dependency prefix (e.g. ~/opt/host-port-deps/usr):
    # off by default, so a plain configure links and runs against the
    # system SDL2/OpenAL/GL. When set, binaries carry an RPATH to it.
    set(MUSASHI_HOST_DEPS_PREFIX "" CACHE PATH
        "Private dependency prefix whose lib/x86_64-linux-gnu is linked and RPATHed (empty: system libraries)")
    if(MUSASHI_HOST_DEPS_PREFIX)
        set(MUSASHI_HOST_DEPS_LIB "${MUSASHI_HOST_DEPS_PREFIX}/lib/x86_64-linux-gnu")
        if(NOT EXISTS "${MUSASHI_HOST_DEPS_LIB}")
            message(FATAL_ERROR "MUSASHI_HOST_DEPS_PREFIX: ${MUSASHI_HOST_DEPS_LIB} does not exist")
        endif()
        link_directories("${MUSASHI_HOST_DEPS_LIB}")
        add_link_options("-Wl,--disable-new-dtags" "-Wl,-rpath,${MUSASHI_HOST_DEPS_LIB}")
    endif()
    find_package(SDL2 REQUIRED)
    find_package(OpenAL REQUIRED)
    # GLAD loads GL entry points at runtime, but the library must still link.
    find_package(OpenGL REQUIRED)

    # src/psx holds the Psy-Q implementations (LIBGPU, LIBGTE, LIBCD, ...) in
    # files named with an uppercase .C. Globbing only "*.c" is case-sensitive on
    # Linux and silently drops all eleven, producing a library that links but
    # exports none of the Psy-Q API. Upstream's own CMakeLists.txt has this bug;
    # REDRIVER2 builds with premake instead, which is why it goes unnoticed.
    #
    # GCC treats .C as C++, so the language is forced back to C to keep these
    # symbols unmangled and callable from the decomp's C.
    file(GLOB_RECURSE MUSASHI_PSYCROSS_SOURCES_C
        "${MUSASHI_PSYCROSS_DIR}/src/*.c"
        "${MUSASHI_PSYCROSS_DIR}/src/*.C"
    )
    file(GLOB_RECURSE MUSASHI_PSYCROSS_SOURCES_CXX
        "${MUSASHI_PSYCROSS_DIR}/src/*.cpp"
    )
    if(NOT MUSASHI_PSYCROSS_SOURCES_C MATCHES "LIBGPU")
        message(FATAL_ERROR
            "PsyCross source glob did not pick up src/psx/LIBGPU.C; the Psy-Q "
            "layer would be missing from the build")
    endif()
    # Keep the ignored upstream checkout intact. Both native and source-step
    # probes use this exact, count-checked signed-translation adaptation.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(MUSASHI_GTE_UPSTREAM "${MUSASHI_PSYCROSS_DIR}/src/gte/PsyX_GTE.cpp")
    set(MUSASHI_GTE_PREPARED "${CMAKE_CURRENT_BINARY_DIR}/generated/PsyX_GTE.cpp")
    add_custom_command(OUTPUT "${MUSASHI_GTE_PREPARED}"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_psycross_gte.py"
            "${MUSASHI_GTE_UPSTREAM}" "${MUSASHI_GTE_PREPARED}"
        DEPENDS "${MUSASHI_GTE_UPSTREAM}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_psycross_gte.py"
        VERBATIM)
    list(REMOVE_ITEM MUSASHI_PSYCROSS_SOURCES_CXX "${MUSASHI_GTE_UPSTREAM}")
    list(APPEND MUSASHI_PSYCROSS_SOURCES_CXX "${MUSASHI_GTE_PREPARED}")
    set_source_files_properties(${MUSASHI_PSYCROSS_SOURCES_C} PROPERTIES LANGUAGE C)
    add_library(psycross_static STATIC
        ${MUSASHI_PSYCROSS_SOURCES_C}
        ${MUSASHI_PSYCROSS_SOURCES_CXX}
    )
    set_target_properties(psycross_static PROPERTIES OUTPUT_NAME "psycross")
    target_include_directories(psycross_static
        PRIVATE "${MUSASHI_PSYCROSS_DIR}/src/gte"
        PUBLIC
            "${MUSASHI_PSYCROSS_DIR}/include"
            "${MUSASHI_PSYCROSS_DIR}/include/psx"
            ${SDL2_INCLUDE_DIRS}
    )
    target_compile_features(psycross_static PUBLIC cxx_std_11)
    # The primitive-tag pointer macros (setaddr, getaddr, nextPrim, isendprim)
    # are gated on USE_EXTENDED_PRIM_POINTERS, not on the 64-bit check that
    # sizes the tag struct. Without it, on x86-64, a 12-byte tag holding a
    # uintptr_t is filled by macros that truncate every pointer to 32 bits and
    # test for a 24-bit terminator, so an ordering-table walk yields nothing.
    # Upstream sets this in premake. PUBLIC because the macros expand in our
    # code as well as in the library.
    # USE_PGXP defaults to USE_EXTENDED_PRIM_POINTERS, so the line above would
    # also switch PGXP on. It is held off explicitly: the port has no GTE
    # transforms yet, and with PGXP on a primitive without a cache entry has
    # its positions zeroed rather than left alone. Revisit when 3D lands.
    target_compile_definitions(psycross_static PUBLIC USE_EXTENDED_PRIM_POINTERS=1 USE_PGXP=0)
    # Per-function sections so an executable links only the PsyCross code it
    # calls: LIBGTE's rsin/rcos table equals the game's own, and must not be
    # carried unless something uses it (retail_content_guard).
    target_compile_options(psycross_static PRIVATE -ffunction-sections -fdata-sections)
    # Two Linux/GCC build breaks in upstream, corrected here rather than by
    # patching the ignored checkout. Upstream is built with MSVC, where neither
    # surfaces.
    #  - Brace-initialised narrowing: PsyX_GPU.cpp initialises a uintptr_t from
    #    -1, and gte/rcossin_tbl.h fills short tables with values above
    #    SHRT_MAX, relying on implementation-defined wraparound. GCC 16 rejects
    #    both, in C as well as C++ now that C23 is the default, so the
    #    diagnostic is downgraded for this target in both languages.
    #  - include/psx ships a Psy-Q compatibility strings.h stub. Being on the
    #    -I path it shadows glibc's, so strcasecmp is never declared even though
    #    PsyX_main.cpp includes the header. pc_port/compat/psycross_compat.h
    #    declares the prototype directly; see that file for the full account.
    #  - LIBETC.C returns a callback pointer as an int (`(int)vsync_callback`),
    #    which truncates on x86-64. GCC 14+ makes that an error in C. It is
    #    downgraded to build, but the truncation is real: the handle returned by
    #    ResetCallback/VSyncCallback must not be cast back to a pointer on a
    #    64-bit host. See docs/PC-PORT.md.
    target_compile_options(psycross_static PRIVATE
        -Wno-narrowing
        -fpermissive
        -include "${CMAKE_CURRENT_SOURCE_DIR}/pc_port/compat/psycross_compat.h"
    )
    target_link_libraries(psycross_static
        PUBLIC
            SDL2::SDL2
            OpenAL::OpenAL
            OpenGL::GL
    )
endif()

if(MUSASHI_BUILD_PC_PORT)
    # Native entry-data loading verifies the complete retail EXE before writes.
    # Crypto only: no TLS/network dependency is used by the port.
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(MUSASHI_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${MUSASHI_GENERATED_DIR}")
    # Run off the disc: the interpreter fetches every main-exe and overlay
    # word from guest RAM loaded from the user's SLUS-00726 image at run time.
    # Nothing is extracted from src/ word exports or the disc at configure time.

    # Smoke-matched leaves: house-style address-named files under src/main/.
    # These correspond to Druthulu vendor/bfm-decomp/src/800.c symbols, but the
    # cleaned local bodies win over giant-TU / MIPS-asm forms. See VENDOR.md
    # and docs/VENDOR-CLEANUP.md. Do not compile vendor src/800.c wholesale.
    set(MUSASHI_SMOKE_MATCHED_SOURCES
        src/main/80012ab0.c
        src/main/80012e6c.c
        src/main/8001311c.c
        src/main/80013154.c
    )
    if(MUSASHI_USE_VENDOR_BFM)
        set(_musashi_vendor_root "${CMAKE_CURRENT_SOURCE_DIR}/vendor/bfm-decomp")
        if(NOT EXISTS "${_musashi_vendor_root}/src/800.c")
            message(FATAL_ERROR
                "MUSASHI_USE_VENDOR_BFM=ON but ${_musashi_vendor_root}/src/800.c is missing. "
                "Restore the vendor sidecar (rsync from _refs/BFM-decomp or branch vendor/bfm-decomp).")
        endif()
        message(STATUS
            "MUSASHI_USE_VENDOR_BFM: smoke uses cleaned src/main leaves "
            "(80012AB0/80012E6C/8001311C/80013154); raw vendor tree at vendor/bfm-decomp/")
    endif()

    add_library(musashi_shared STATIC
        src/shared/runtime_api.c
        # First native consumer of verified decomp-owned code. Independently
        # byte-matched; does not claim the complete PS1 gameplay graph is portable.
        ${MUSASHI_SMOKE_MATCHED_SOURCES}
    )
    target_include_directories(musashi_shared
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/shared
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(musashi_shared PUBLIC c_std_99)
    if(MUSASHI_USE_VENDOR_BFM)
        target_compile_definitions(musashi_shared PUBLIC MUSASHI_USE_VENDOR_BFM=1)
    endif()

    if(WIN32)
        set(MUSASHI_PLATFORM_SOURCE pc_port/platform/windows.c)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(MUSASHI_PLATFORM_SOURCE pc_port/platform/linux.c)
    else()
        message(FATAL_ERROR
            "Unsupported target platform '${CMAKE_SYSTEM_NAME}'; provide a pc_port/platform adapter")
    endif()

    add_library(musashi_pc_port STATIC
        pc_port/port_runtime.c
        pc_port/boot_memory.c
        pc_port/startup_bindings.c
        pc_port/mips_formatter.c
        pc_port/gpu_bindings.c
        pc_port/device_reset_bindings.c
        pc_port/callback_bindings.c
        pc_port/event_bindings.c
        pc_port/cd_controller.c
        pc_port/cd_dma3.c
        pc_port/gpu_dma2.c
        pc_port/dma_otc.c
        pc_port/disc_media.c
        pc_port/spu_cd_audio.c
        pc_port/irq_controller.c
        pc_port/dma_controller.c
        pc_port/gpu_controller.c
        pc_port/mdec_controller.c
        pc_port/bios_events.c
        pc_port/bios_kernel.c
        pc_port/cpu_status.c
        pc_port/bios_exception.c
        pc_port/bios_heap.c
        pc_port/bios_card.c
        pc_port/bios_backup_unit.c
        pc_port/bios_input.c
        pc_port/source_clock.c
        pc_port/timer2.c
        pc_port/device_epoch.c
        pc_port/sio_controller.c
        pc_port/scanline_timer.c
        pc_port/irq_policy.c
        pc_port/libetc_irq_registration.c
        pc_port/libetc_dma_registration.c
        pc_port/libetc_irq_veneer.c
        pc_port/libetc_dma_veneer.c
        src/main/8001096c.c
        pc_port/libetc_vsync_handler.c
        pc_port/libetc_dma_irq.c
        pc_port/libetc_irq_dispatch.c
        pc_port/libmcrd_vblank.c
        
        pc_port/vblank_bindings.c
        src/main/80016714.c
        src/main/80029044.c
        src/main/8002906c.c
        ${MUSASHI_PLATFORM_SOURCE}
    )
    # Only this decomp-owned translation unit receives the native word binding.
    # Retail compiler/oracle builds do not define it and retain the original ABI.
    set_source_files_properties(src/main/8001096c.c PROPERTIES
        COMPILE_DEFINITIONS MUSASHI_NATIVE_CALLBACK_HANDLER=1)
    set_source_files_properties(src/main/80016714.c PROPERTIES
        COMPILE_DEFINITIONS MUSASHI_NATIVE_MEMSET_BINDING=1)
    set_source_files_properties(src/main/80029044.c src/main/8002906c.c PROPERTIES
        COMPILE_DEFINITIONS MUSASHI_NATIVE_TABLE_CLEAR_BINDING=1)
    target_include_directories(musashi_pc_port
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/pc_port
            ${MUSASHI_GENERATED_DIR}
    )
    target_compile_features(musashi_pc_port PUBLIC c_std_99)
    target_link_libraries(musashi_pc_port PUBLIC musashi_shared PRIVATE OpenSSL::Crypto)
    if(MUSASHI_WITH_PSYCROSS)
        target_sources(musashi_pc_port PRIVATE
            pc_port/psyx_shutdown.cpp
            pc_port/psyq_probe.c
            pc_port/irq_scheduler.c
            pc_port/gpu_psycross.c
            pc_port/gte_owner.c
            pc_port/audio_sdl.c
        )
        target_link_libraries(musashi_pc_port PUBLIC psycross_static)
        target_compile_definitions(musashi_pc_port PUBLIC MUSASHI_WITH_PSYCROSS=1)
    endif()

    add_executable(musashi_pc_smoke pc_port/main.c)
    target_compile_features(musashi_pc_smoke PRIVATE c_std_99)
    target_link_libraries(musashi_pc_smoke PRIVATE musashi_pc_port)

    enable_testing()
    add_test(NAME musashi_pc_smoke COMMAND musashi_pc_smoke)
    add_executable(musashi_entry_cpu_probe tests/entry_cpu_probe.c)
    target_compile_features(musashi_entry_cpu_probe PRIVATE c_std_99)
    target_link_libraries(musashi_entry_cpu_probe PRIVATE musashi_pc_port)
    add_test(NAME entry_cpu_boundary COMMAND musashi_entry_cpu_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/extracted/disc/files/SLUS_007.26)
    set_tests_properties(entry_cpu_boundary PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(musashi_boot_data_probe tests/boot_data_probe.c)
    target_compile_features(musashi_boot_data_probe PRIVATE c_std_99)
    target_link_libraries(musashi_boot_data_probe PRIVATE musashi_pc_port)
    add_test(NAME boot_data_boundary COMMAND musashi_boot_data_probe)

    add_executable(musashi_gpu_device_probe tests/gpu_device_probe.c)
    target_compile_features(musashi_gpu_device_probe PRIVATE c_std_99)
    target_link_libraries(musashi_gpu_device_probe PRIVATE musashi_pc_port)
    add_test(NAME gpu_device_boundary COMMAND musashi_gpu_device_probe)

    add_executable(musashi_device_reset_probe tests/device_reset_probe.c)
    target_compile_features(musashi_device_reset_probe PRIVATE c_std_99)
    target_link_libraries(musashi_device_reset_probe PRIVATE musashi_pc_port)
    add_test(NAME device_reset_boundary COMMAND musashi_device_reset_probe)

    add_executable(musashi_word_clear_probe tests/word_clear_probe.c)
    target_compile_features(musashi_word_clear_probe PRIVATE c_std_99)
    target_link_libraries(musashi_word_clear_probe PRIVATE musashi_pc_port)
    add_test(NAME word_clear_boundary COMMAND musashi_word_clear_probe)

    add_executable(musashi_heap_continuation_probe tests/heap_continuation_probe.c)
    target_compile_features(musashi_heap_continuation_probe PRIVATE c_std_99)
    target_include_directories(musashi_heap_continuation_probe PRIVATE "${MUSASHI_GENERATED_DIR}")
    target_link_libraries(musashi_heap_continuation_probe PRIVATE musashi_pc_port)
    add_test(NAME heap_continuation_source_cpu COMMAND musashi_heap_continuation_probe
        ${CMAKE_CURRENT_SOURCE_DIR}/extracted/disc/files/SLUS_007.26)
    set_tests_properties(heap_continuation_source_cpu PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(musashi_event_registration_cpu_probe tests/event_registration_cpu_probe.c)
    target_compile_features(musashi_event_registration_cpu_probe PRIVATE c_std_99)
    target_include_directories(musashi_event_registration_cpu_probe PRIVATE "${MUSASHI_GENERATED_DIR}")
    target_link_libraries(musashi_event_registration_cpu_probe PRIVATE musashi_pc_port)
    add_test(NAME event_registration_source_cpu COMMAND musashi_event_registration_cpu_probe)

    add_executable(musashi_dev_menu_probe tests/dev_menu_probe.c pc_port/dev_menu.c)
    target_compile_features(musashi_dev_menu_probe PRIVATE c_std_99)
    target_include_directories(musashi_dev_menu_probe PRIVATE pc_port/include)
    add_test(NAME dev_menu_model COMMAND musashi_dev_menu_probe)

    add_executable(musashi_mips_formatter_probe tests/mips_formatter_probe.c)
    target_compile_features(musashi_mips_formatter_probe PRIVATE c_std_99)
    target_link_libraries(musashi_mips_formatter_probe PRIVATE musashi_pc_port)
    add_test(NAME mips_formatter_boundary COMMAND musashi_mips_formatter_probe)

    add_executable(musashi_callback_registration_probe tests/callback_registration_probe.c)
    target_compile_features(musashi_callback_registration_probe PRIVATE c_std_99)
    target_link_libraries(musashi_callback_registration_probe PRIVATE musashi_pc_port)
    add_test(NAME callback_registration_startup
        COMMAND musashi_callback_registration_probe
                ${CMAKE_CURRENT_SOURCE_DIR}/extracted/disc/files/SLUS_007.26)

    add_executable(musashi_irq_controller_probe tests/irq_controller_probe.c)
    target_compile_features(musashi_irq_controller_probe PRIVATE c_std_99)
    target_link_libraries(musashi_irq_controller_probe PRIVATE musashi_pc_port)
    add_test(NAME irq_controller_boundary COMMAND musashi_irq_controller_probe)

    if(MUSASHI_WITH_PSYCROSS)
        # Own pinned entry state and IRQ lifetime in an executable. This
        # currently stops at the first missing device; it is not a menu demo.
        add_library(musashi_x11_nograb SHARED pc_port/x11_nograb.c)
        target_include_directories(musashi_x11_nograb PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/include)
        target_link_libraries(musashi_x11_nograb PRIVATE ${CMAKE_DL_LIBS})
        add_executable(musashi_native_boot pc_port/native_boot.c)
        target_link_options(musashi_native_boot PRIVATE -Wl,--gc-sections)
        target_compile_features(musashi_native_boot PRIVATE c_std_99)
        target_link_libraries(musashi_native_boot PRIVATE musashi_pc_port musashi_x11_nograb ${CMAKE_DL_LIBS})

        add_executable(musashi_gpu_psycross_probe tests/gpu_psycross_probe.c)
        target_compile_features(musashi_gpu_psycross_probe PRIVATE c_std_99)
        target_link_libraries(musashi_gpu_psycross_probe PRIVATE musashi_pc_port)
        add_test(NAME gpu_psycross_live COMMAND musashi_gpu_psycross_probe)
        set_tests_properties(gpu_psycross_live PROPERTIES SKIP_RETURN_CODE 77)

        # The probe supplies a test-owned VSyncCallback. Production resolves
        # the same bridge against PsyCross's actual VBlank event source.
        add_executable(musashi_irq_scheduler_probe
            tests/irq_scheduler_probe.c
            pc_port/irq_scheduler.c
            pc_port/cpu_status.c
            pc_port/mips_formatter.c
            pc_port/irq_controller.c
            pc_port/boot_memory.c
            pc_port/libetc_irq_dispatch.c
            pc_port/libetc_vsync_handler.c
            src/main/8001096c.c
            pc_port/libetc_dma_irq.c
            pc_port/libetc_dma_registration.c
            pc_port/libmcrd_vblank.c
            
        )
        target_compile_features(musashi_irq_scheduler_probe PRIVATE c_std_99)
        target_compile_definitions(musashi_irq_scheduler_probe PRIVATE
            MUSASHI_NATIVE_IRQ_DISPATCH=1
            MUSASHI_NATIVE_CALLBACK_HANDLER=1
            MUSASHI_NATIVE_DMA_IRQ_HANDLER=1
            MUSASHI_NATIVE_VBLANK=1
        )
        target_include_directories(musashi_irq_scheduler_probe PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/include
            ${MUSASHI_GENERATED_DIR}
        )
        # Link the real CD executor without importing the PsyCross worker
        # into this synthetic source-registration fixture.
        target_compile_options(musashi_irq_scheduler_probe PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(musashi_irq_scheduler_probe PRIVATE -Wl,--gc-sections)
        target_link_libraries(musashi_irq_scheduler_probe PRIVATE OpenSSL::Crypto SDL2::SDL2)
        add_test(NAME irq_scheduler_boundary COMMAND musashi_irq_scheduler_probe)

        # This one requires a real SDL/GL display because PsyCross owns the
        # VBlank thread. It returns 77 when no display can be initialized.
        add_executable(musashi_irq_scheduler_live_probe tests/irq_scheduler_live_probe.c)
        target_compile_features(musashi_irq_scheduler_live_probe PRIVATE c_std_99)
        target_link_libraries(musashi_irq_scheduler_live_probe PRIVATE musashi_pc_port)
        add_test(NAME irq_scheduler_live COMMAND musashi_irq_scheduler_live_probe)
        set_tests_properties(irq_scheduler_live PROPERTIES SKIP_RETURN_CODE 77)

        add_executable(musashi_input_irq_live_probe tests/input_irq_live_probe.c)
        target_compile_features(musashi_input_irq_live_probe PRIVATE c_std_99)
        target_link_libraries(musashi_input_irq_live_probe PRIVATE musashi_pc_port)
        add_test(NAME input_irq_live COMMAND musashi_input_irq_live_probe
            ${CMAKE_CURRENT_SOURCE_DIR}/extracted/disc/files/SLUS_007.26)
        set_tests_properties(input_irq_live PROPERTIES SKIP_RETURN_CODE 77)

        add_executable(musashi_card_irq_live_probe tests/card_irq_live_probe.c)
        target_compile_features(musashi_card_irq_live_probe PRIVATE c_std_99)
        target_link_libraries(musashi_card_irq_live_probe PRIVATE musashi_pc_port)
        add_test(NAME card_irq_live COMMAND musashi_card_irq_live_probe)
        set_tests_properties(card_irq_live PROPERTIES SKIP_RETURN_CODE 77)

        add_test(NAME callback_registration_live
            COMMAND musashi_callback_registration_probe
                    ${CMAKE_CURRENT_SOURCE_DIR}/extracted/disc/files/SLUS_007.26 --live)
        set_tests_properties(callback_registration_live PROPERTIES SKIP_RETURN_CODE 77)

        # Synthetic checks of retail startup's GTE command/register boundary.
        add_executable(musashi_startup_gte_probe tests/startup_gte_probe.c)
        target_compile_features(musashi_startup_gte_probe PRIVATE c_std_99)
        target_link_libraries(musashi_startup_gte_probe PRIVATE psycross_static)
        add_test(NAME startup_gte_probe COMMAND musashi_startup_gte_probe)
        add_test(NAME startup_gte_wrong_register
                 COMMAND musashi_startup_gte_probe --wrong-v0)
        set_tests_properties(startup_gte_wrong_register PROPERTIES WILL_FAIL TRUE)

        # Draws one quad through the Psy-Q layer and reports sampled pixels.
        # The verdict is made by tools/render_check.py so the comparison stays
        # unit-tested; the smoke test above stays display-free on purpose.
        add_executable(musashi_render_quad pc_port/render_quad.c)
        target_compile_features(musashi_render_quad PRIVATE c_std_99)
        target_link_libraries(musashi_render_quad PRIVATE musashi_pc_port)

        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        add_test(NAME native_startup_boundary
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/native_boot_check.py
                    $<TARGET_FILE:musashi_native_boot>
                    ${CMAKE_CURRENT_SOURCE_DIR}/extracted/disc/files/SLUS_007.26)
        set_tests_properties(native_startup_boundary PROPERTIES SKIP_RETURN_CODE 77)
        add_test(
            NAME render_quad
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/render_check.py
                    $<TARGET_FILE:musashi_render_quad>
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        # Without xvfb-run the check exits 77, which ctest reports as skipped
        # rather than passed vacuously or failed spuriously.
        set_tests_properties(render_quad PROPERTIES SKIP_RETURN_CODE 77)

        # Draws a real model from the disc. Needs the extracted model, which is
        # retail-derived and untracked, so the test is registered only when it
        # is present and is otherwise skipped rather than failed.
        add_executable(musashi_render_tmd pc_port/render_tmd.c)
        target_compile_features(musashi_render_tmd PRIVATE c_std_99)
        target_link_libraries(musashi_render_tmd PRIVATE musashi_pc_port)

        set(MUSASHI_TMD_MODEL ${CMAKE_CURRENT_SOURCE_DIR}/extracted/models/sc01_a97000.tmd)
        add_test(
            NAME render_tmd
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/render_check.py
                    $<TARGET_FILE:musashi_render_tmd>
                    --argument ${MUSASHI_TMD_MODEL}
                    --expect-inside 240,197,152
                    --expect-outside 0,0,255
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(render_tmd PROPERTIES SKIP_RETURN_CODE 77)
        if(NOT EXISTS ${MUSASHI_TMD_MODEL})
            set_tests_properties(render_tmd PROPERTIES DISABLED TRUE)
            message(STATUS
                "render_tmd disabled: extract the model first with "
                "tools/extract_tmd.py --offset 0xA97000")
        endif()
    endif()

    # Platform layer (pc_port/platform, from port/platform-layer): game-facing
    # bfm_plat_* interfaces, backends, mods/plugins, PsyQ compat for the lane.
    # Core: C99, no dependencies. dlopen for native plugins.
    # Core list tracks pc_port/platform/bfm_plat*.c (ARCHITECTURE-PORT.md).
    file(GLOB BFM_PLAT_CORE_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/platform/bfm_plat*.c)
    add_library(bfm_plat STATIC ${BFM_PLAT_CORE_SOURCES}
        pc_port/platform/psyq/bfm_psyq_compat.c
        pc_port/platform/psyq/bfm_psyq_libgs.c
        pc_port/platform/psyq/bfm_psyq_libgte.c)        # + psyq/psyq_psycross.c with PsyCross
    target_include_directories(bfm_plat PUBLIC pc_port/platform)
    target_link_libraries(bfm_plat PUBLIC ${CMAKE_DL_LIBS} m)

    if(MUSASHI_WITH_PSYCROSS)   # SDL2 / OpenAL / GL already found above
        target_sources(bfm_plat PRIVATE
            pc_port/platform/backends/renderer_psycross.c
            pc_port/platform/backends/audio_sdl.c
            pc_port/platform/backends/input_sdl.c
            pc_port/platform/psyq/psyq_psycross.c)   # PsyQ compat wrappers on PsyCross
        # PsyCross headers need C11 (static_assert)
        set_source_files_properties(pc_port/platform/backends/renderer_psycross.c
            pc_port/platform/psyq/psyq_psycross.c
            PROPERTIES COMPILE_OPTIONS "-std=gnu11;-include;assert.h")
        target_include_directories(bfm_plat PRIVATE
            ${MUSASHI_PSYCROSS_DIR}/include pc_port/include)
        target_compile_definitions(bfm_plat PRIVATE BFM_PLAT_WITH_PSYCROSS
            BFM_PLAT_WITH_SDL_AUDIO BFM_PLAT_WITH_SDL_INPUT)
        # OpenAL output backend (found above for PsyCross): MUSASHI_AUDIO=bfm_plat
        if(OPENAL_FOUND)
            target_sources(bfm_plat PRIVATE pc_port/platform/backends/audio_openal.c)
            target_include_directories(bfm_plat PRIVATE ${OPENAL_INCLUDE_DIR}/..)
            target_compile_definitions(bfm_plat PRIVATE BFM_PLAT_WITH_OPENAL)
            target_link_libraries(bfm_plat PRIVATE ${OPENAL_LIBRARY})
        endif()
        target_link_libraries(bfm_plat PRIVATE SDL2::SDL2 musashi_pc_port)
    endif()

    find_package(ZLIB)          # optional: .png texture replacements
    if(ZLIB_FOUND)
        target_sources(bfm_plat PRIVATE pc_port/platform/backends/texture_png.c)
        target_compile_definitions(bfm_plat PRIVATE BFM_PLAT_WITH_PNG)
        target_link_libraries(bfm_plat PRIVATE ZLIB::ZLIB)
    endif()
    # Optional Lua (after tools/fetch_lua.sh): all of tools/third_party/lua/src/*.c
    # except lua.c and luac.c, as a separate C target without -Werror.
    set(BFM_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/third_party/lua/src)
    if(EXISTS ${BFM_LUA_DIR}/lua.h)
        file(GLOB BFM_LUA_SOURCES ${BFM_LUA_DIR}/*.c)
        list(FILTER BFM_LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
        add_library(bfm_lua STATIC ${BFM_LUA_SOURCES})
        target_include_directories(bfm_lua PUBLIC ${BFM_LUA_DIR})
        target_sources(bfm_plat PRIVATE pc_port/platform/backends/script_lua.c)
        target_compile_definitions(bfm_plat PRIVATE BFM_PLAT_WITH_LUA)
        target_link_libraries(bfm_plat PRIVATE bfm_lua)
    endif()
    # Optional: pc_port/platform/backends/disc_pinned.c + BFM_PLAT_WITH_PINNED_DISC
    # (links disc_media.c / OpenSSL::Crypto, already in musashi_pc_port).

    # Optional gl renderer: needs SDL2 and the Khronos GL headers only (entry
    # points come from SDL_GL_GetProcAddress; do not link libGL or a loader).
    find_path(BFM_GLCOREARB_DIR GL/glcorearb.h)
    if(TARGET SDL2::SDL2 AND BFM_GLCOREARB_DIR)
        target_sources(bfm_plat PRIVATE
            pc_port/platform/backends/renderer_gl.c
            pc_port/platform/backends/gl/bfm_gl_core.c
            pc_port/platform/backends/gl/bfm_gl_exec.c)
        target_include_directories(bfm_plat PRIVATE ${BFM_GLCOREARB_DIR})
        target_compile_definitions(bfm_plat PRIVATE BFM_PLAT_WITH_GL)
        target_link_libraries(bfm_plat PRIVATE SDL2::SDL2)
    endif()

    if(TARGET musashi_native_boot)
        target_link_libraries(musashi_native_boot PRIVATE bfm_plat)
        target_compile_definitions(musashi_native_boot PRIVATE MUSASHI_WITH_BFM_PLAT=1)
        target_sources(musashi_native_boot PRIVATE pc_port/plat_audio_sink.c pc_port/dev_menu.c)
    endif()
    # SPU core -> plat_audio_sink -> bfm_plat null sink, in-process: a CD tone
    # and an ADPCM voice (SPU RAM upload, KON) must reach the sink non-silent.
    add_executable(musashi_plat_audio_sink_probe tests/plat_audio_sink_probe.c
        pc_port/plat_audio_sink.c)
    target_include_directories(musashi_plat_audio_sink_probe PRIVATE pc_port/include)
    target_link_libraries(musashi_plat_audio_sink_probe PRIVATE bfm_plat musashi_pc_port)
    add_test(NAME plat_audio_sink COMMAND musashi_plat_audio_sink_probe)

    if(MUSASHI_NATIVE_LANE)
        find_program(MUSASHI_CLANG NAMES clang REQUIRED)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(MUSASHI_LANE_DIR ${MUSASHI_GENERATED_DIR}/native_lane)
        # Overlay members compiled into the lane (digest-verified per call).
        set(MUSASHI_LANE_OVERLAY_MEMBERS "main_0003" CACHE STRING
            "comma-separated overlay members for the native lane")
        string(REPLACE "," ";" MUSASHI_LANE_OVERLAY_LIST "${MUSASHI_LANE_OVERLAY_MEMBERS}")
        set(MUSASHI_LANE_OVERLAY_GLOBS)
        foreach(member IN LISTS MUSASHI_LANE_OVERLAY_LIST)
            list(APPEND MUSASHI_LANE_OVERLAY_GLOBS
                ${CMAKE_CURRENT_SOURCE_DIR}/src/overlays/${member}/*.c)
        endforeach()
        file(GLOB MUSASHI_LANE_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/src/main/*.c ${MUSASHI_LANE_OVERLAY_GLOBS})
        # hooked_pcs() reads the port's sources; psyq_hooked() the compat table.
        file(GLOB MUSASHI_LANE_HOOK_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/*.c)
        # Release builds: native code only from owner-authored matched C.
        option(MUSASHI_LANE_OWN_CODE_ONLY "Native lane compiles only owner-authored C" OFF)
        set(MUSASHI_LANE_OWN_CODE_FLAG)
        if(MUSASHI_LANE_OWN_CODE_ONLY)
            set(MUSASHI_LANE_OWN_CODE_FLAG --own-code-only
                --only ${CMAKE_CURRENT_SOURCE_DIR}/config/lane_own_code_allow.txt)
        endif()
        add_custom_command(
            OUTPUT ${MUSASHI_LANE_DIR}/native_lane_table.c
                   ${MUSASHI_LANE_DIR}/libmusashi_native_lane.a
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/native_lane_gen.py
                    --out ${MUSASHI_LANE_DIR} --clang ${MUSASHI_CLANG} --ar ${CMAKE_AR} --jobs 4
                    --overlay-members "${MUSASHI_LANE_OVERLAY_MEMBERS}"
                    ${MUSASHI_LANE_OWN_CODE_FLAG}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/native_lane_gen.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/provenance/matches.json
                    ${MUSASHI_LANE_SOURCES}
                    ${MUSASHI_LANE_HOOK_SOURCES}
                    ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/platform/psyq/bfm_psyq_compat.def
            COMMENT "Checking and compiling native-lane decomp C"
            VERBATIM)
        add_custom_target(musashi_native_lane_gen
            DEPENDS ${MUSASHI_LANE_DIR}/native_lane_table.c
                    ${MUSASHI_LANE_DIR}/libmusashi_native_lane.a)
        add_library(musashi_native_lane_funcs STATIC IMPORTED)
        set_target_properties(musashi_native_lane_funcs PROPERTIES
            IMPORTED_LOCATION ${MUSASHI_LANE_DIR}/libmusashi_native_lane.a)
        add_dependencies(musashi_native_lane_funcs musashi_native_lane_gen)
        # + port-plat's PsyQ compat table as lane entries (native_lane_psyq.c)
        add_library(musashi_native_lane STATIC ${MUSASHI_LANE_DIR}/native_lane_table.c
            pc_port/native_lane_psyq.c)
        target_link_libraries(musashi_native_lane PUBLIC musashi_pc_port musashi_native_lane_funcs
            bfm_plat)
        target_include_directories(musashi_native_lane PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/pc_port/include)

        add_executable(musashi_native_lane_overlay_probe tests/native_lane_overlay_probe.c)
        target_compile_features(musashi_native_lane_overlay_probe PRIVATE c_std_99)
        target_link_libraries(musashi_native_lane_overlay_probe PRIVATE musashi_native_lane)
        add_executable(musashi_native_lane_probe tests/native_lane_probe.c)
        target_compile_features(musashi_native_lane_probe PRIVATE c_std_99)
        target_link_libraries(musashi_native_lane_probe PRIVATE musashi_native_lane)
        add_test(NAME native_lane_differential
            COMMAND musashi_native_lane_probe
                    ${CMAKE_CURRENT_SOURCE_DIR}/extracted/disc/files/SLUS_007.26)
        set_tests_properties(native_lane_differential PROPERTIES SKIP_RETURN_CODE 77)

        if(TARGET musashi_native_boot)
            target_compile_definitions(musashi_native_boot PRIVATE MUSASHI_NATIVE_LANE_TABLE=1)
            target_link_libraries(musashi_native_boot PRIVATE musashi_native_lane)
        endif()
    endif()

    # Probes seed synthetic guest RAM; they run the retail words from the
    # developer's own extracted EXE (never from src/ or the build tree).
    set(MUSASHI_CODE_IMAGE ${CMAKE_CURRENT_SOURCE_DIR}/extracted/disc/files/SLUS_007.26)
    if(EXISTS ${MUSASHI_CODE_IMAGE})
        get_property(MUSASHI_ALL_TESTS DIRECTORY PROPERTY TESTS)
        foreach(MUSASHI_TEST IN LISTS MUSASHI_ALL_TESTS)
            set_property(TEST ${MUSASHI_TEST} APPEND PROPERTY
                ENVIRONMENT "MUSASHI_CODE_IMAGE=${MUSASHI_CODE_IMAGE}")
        endforeach()
    endif()

    # No Sony PsyQ code in the port: fail configuration when a decomp source
    # in a PsyQ range (config/psyq_ranges.txt) is compiled into any target,
    # except the listed bindings still pending PsyCross/HLE replacement.
    get_property(MUSASHI_PSYQ_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(MUSASHI_PSYQ_SOURCES)
    foreach(MUSASHI_TARGET IN LISTS MUSASHI_PSYQ_TARGETS)
        get_target_property(MUSASHI_TARGET_TYPE ${MUSASHI_TARGET} TYPE)
        if(MUSASHI_TARGET_TYPE STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        get_target_property(MUSASHI_TARGET_SOURCES ${MUSASHI_TARGET} SOURCES)
        if(MUSASHI_TARGET_SOURCES)
            foreach(MUSASHI_SOURCE IN LISTS MUSASHI_TARGET_SOURCES)
                if(MUSASHI_SOURCE MATCHES "src/main/")
                    list(APPEND MUSASHI_PSYQ_SOURCES ${MUSASHI_SOURCE})
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES MUSASHI_PSYQ_SOURCES)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/psyq_manifest.py check
                --allow ${CMAKE_CURRENT_SOURCE_DIR}/config/psyq_pending_replacement.txt
                ${MUSASHI_PSYQ_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE MUSASHI_PSYQ_RESULT)
    if(NOT MUSASHI_PSYQ_RESULT EQUAL 0)
        message(FATAL_ERROR "Sony PsyQ code would be compiled into the port (see above)")
    endif()

    # The built port must carry no retail content (tools/retail_guard.py over
    # the shipped outputs, against the developer's own disc extract).
    add_test(NAME retail_content_guard
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_port_retail.py
                ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(retail_content_guard PROPERTIES SKIP_RETURN_CODE 77)

    # Every CTest probe states its expectations as assert(). CMake puts -DNDEBUG
    # in the Release/RelWithDebInfo/MinSizeRel flags, which compiles all of them
    # out: the probe then runs to completion and exits 0 having checked nothing,
    # so a whole passing CTest run means only that the binaries did not crash.
    # A probe built that way is not evidence, so assertions are forced back on
    # for the probes alone. The port library and its executables keep the
    # build type's own flags, because their asserts are product behaviour.
    get_property(MUSASHI_DIRECTORY_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(MUSASHI_TARGET IN LISTS MUSASHI_DIRECTORY_TARGETS)
        if(MUSASHI_TARGET MATCHES "_probe$" OR MUSASHI_TARGET STREQUAL "musashi_pc_smoke")
            target_compile_options(${MUSASHI_TARGET} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()
