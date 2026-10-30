# Mayhem layer: wraps the engine (engine/CMakeLists.txt, untouched) so one configure builds
# either the fuzz harnesses (MAYHEM_MODE=fuzz) or the normal-flags test suite plus the golden
# oracle (MAYHEM_MODE=test). Driven by mayhem/build.sh.
cmake_minimum_required(VERSION 3.25)
project(MayhemLayer C CXX)

set(MAYHEM_MODE "fuzz" CACHE STRING "fuzz | test")
set(MAYHEM_FUZZ_ENGINE "-fsanitize=fuzzer" CACHE STRING "link flag providing the libFuzzer main")
set(MAYHEM_STANDALONE_MAIN "" CACHE FILEPATH "run-once driver for the -standalone reproducers")
set(MAYHEM_LSAN_OFF "${CMAKE_CURRENT_LIST_DIR}/lsan_off.cc" CACHE FILEPATH "build-time LeakSanitizer-off hook linked into every fuzz binary")

# POSIX stand-in for <Windows.h>, which octagram_gram.cpp includes unconditionally.
include_directories(BEFORE ${CMAKE_CURRENT_LIST_DIR}/compat)

if(MAYHEM_MODE STREQUAL "test")
    set(BUILD_TESTING ON CACHE BOOL "" FORCE)
else()
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
endif()
include(CTest)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../engine engine)

if(MAYHEM_MODE STREQUAL "fuzz")
    # kenlm's bit_packing.hh deliberately does unaligned 64-bit loads/stores (benign on x86/arm64);
    # without this every valid model trips UBSan's alignment check. Scoped to the kenlm target only.
    target_compile_options(metasequoia_ime_ngram PRIVATE -fno-sanitize=alignment)
    # kenlm FullScore computes `in_state.backoff + ngram_length - 1`, whose intermediate pointer is
    # past the end of the 2-element array (the final value is in range). One file only.
    set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/../engine/ngram/kenlm/lm/model.cc
        TARGET_DIRECTORY metasequoia_ime_ngram PROPERTIES COMPILE_OPTIONS -fno-sanitize=array-bounds)

    # CMake drops -D cache entries when it discards a cache (compiler path changed between the
    # ccache-prefilled image build and a plain re-run), so fall back to the base image's variable.
    if(NOT MAYHEM_STANDALONE_MAIN)
        set(MAYHEM_STANDALONE_MAIN "$ENV{STANDALONE_FUZZ_MAIN}")
    endif()
    add_library(standalone_main OBJECT ${MAYHEM_STANDALONE_MAIN})
    foreach(name sentence_model japanese_model language_model gram session)
        add_executable(fuzz_${name} fuzz_${name}.cc ${MAYHEM_LSAN_OFF})
        target_include_directories(fuzz_${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
        target_link_libraries(fuzz_${name} PRIVATE MetasequoiaIme::Engine)
        target_link_options(fuzz_${name} PRIVATE ${MAYHEM_FUZZ_ENGINE})

        add_executable(fuzz_${name}-standalone fuzz_${name}.cc ${MAYHEM_LSAN_OFF} $<TARGET_OBJECTS:standalone_main>)
        target_include_directories(fuzz_${name}-standalone PRIVATE ${CMAKE_CURRENT_LIST_DIR})
        target_link_libraries(fuzz_${name}-standalone PRIVATE MetasequoiaIme::Engine)
    endforeach()
else()
    add_executable(oracle_test oracle_test.cc)
    target_include_directories(oracle_test PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    target_link_libraries(oracle_test PRIVATE MetasequoiaIme::Engine)
endif()
