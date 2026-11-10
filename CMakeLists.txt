cmake_minimum_required(VERSION 3.24)
project(HeavyWeapon LANGUAGES C CXX)

# Native desktop port for Windows x64 (MSVC) and Linux x64, on SDL3 + legacy OpenGL.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only 64-bit builds are supported (use an x64 toolchain)")
endif()

option(HW_PERF "Enable the perf.log profiler (src/core/Perf.h)" OFF)
set(HW_DATA_DIR "" CACHE PATH "Default game data directory, searched after the usual places")

set(HW_SDL3_VERSION       "3.4.20" CACHE STRING "SDL3 version to download")
set(HW_SDL3_IMAGE_VERSION "3.4.8"  CACHE STRING "SDL3_image version to download")
set(HW_SDL3_MIXER_VERSION "3.2.4"  CACHE STRING "SDL3_mixer version to download")
set(HW_OPENMPT_VERSION    "0.8.10" CACHE STRING "libopenmpt version to download")
set(_gh https://github.com/libsdl-org)

set(SOURCES
    src/main.cpp
    src/platform/Vfs.cpp
    src/platform/AudioSystem.cpp
    src/platform/InputManager.cpp
    src/loader/XmlLoader.cpp
    src/render/TextureManager.cpp
    src/render/Renderer.cpp
    src/render/FontRenderer.cpp
    src/render/WorldRenderer.cpp
    src/core/GameEngine.cpp
    src/core/Perf.cpp
    src/game/Gfx.cpp
    src/game/Board.cpp
    src/game/Crafts.cpp
    src/game/Hazards.cpp
    src/game/Supplies.cpp
    src/game/Weapons.cpp
    src/game/Bosses.cpp
    src/mods/ModSystem.cpp
    src/render/FlashMovie.cpp
    3rdparty/tinyxml2/tinyxml2.cpp
)

# Lua 5.4 for mod scripts (3rdparty/lua, vendored). Only the core and the pure libraries
# the sandbox opens: io, os, package/loadlib and debug are not compiled in at all.
set(_lua 3rdparty/lua)
add_library(hw_lua STATIC
    ${_lua}/lapi.c ${_lua}/lcode.c ${_lua}/lctype.c ${_lua}/ldebug.c ${_lua}/ldo.c
    ${_lua}/ldump.c ${_lua}/lfunc.c ${_lua}/lgc.c ${_lua}/llex.c ${_lua}/lmem.c
    ${_lua}/lobject.c ${_lua}/lopcodes.c ${_lua}/lparser.c ${_lua}/lstate.c ${_lua}/lstring.c
    ${_lua}/ltable.c ${_lua}/ltm.c ${_lua}/lundump.c ${_lua}/lvm.c ${_lua}/lzio.c
    ${_lua}/lauxlib.c ${_lua}/lbaselib.c ${_lua}/lcorolib.c ${_lua}/lmathlib.c
    ${_lua}/lstrlib.c ${_lua}/ltablib.c ${_lua}/lutf8lib.c)
target_include_directories(hw_lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${_lua})
if(MSVC)
    target_compile_options(hw_lua PRIVATE /W0)
else()
    target_compile_options(hw_lua PRIVATE -w)
    target_link_libraries(hw_lua PUBLIC m)
endif()

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# SDL3, SDL3_image, SDL3_mixer
# ---------------------------------------------------------------------------
if(WIN32)
    # Windows: the official prebuilt "devel VC" packages, downloaded once into the build
    # tree. Each ships a CMake config package; its DLLs are copied next to the exe.
    if(NOT MSVC)
        message(FATAL_ERROR "The Windows build uses MSVC (see tools/windows/build.cmd)")
    endif()
    set(_deps ${CMAKE_BINARY_DIR}/_deps)
    function(hw_fetch_vc var repo pkg ver)
        set(_dir ${_deps}/${pkg}-${ver})
        if(NOT EXISTS ${_dir}/cmake)
            set(_zip ${_deps}/${pkg}-devel-${ver}-VC.zip)
            message(STATUS "Downloading ${pkg} ${ver}")
            file(DOWNLOAD ${_gh}/${repo}/releases/download/release-${ver}/${pkg}-devel-${ver}-VC.zip
                 ${_zip} STATUS _st TLS_VERIFY ON)
            list(GET _st 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE ${_zip})
                message(FATAL_ERROR "Failed to download ${pkg} ${ver}: ${_st}")
            endif()
            file(ARCHIVE_EXTRACT INPUT ${_zip} DESTINATION ${_deps})
            file(REMOVE ${_zip})
        endif()
        set(${var} ${_dir} PARENT_SCOPE)
    endfunction()
    hw_fetch_vc(_sdl3       SDL       SDL3       ${HW_SDL3_VERSION})
    hw_fetch_vc(_sdl3_image SDL_image SDL3_image ${HW_SDL3_IMAGE_VERSION})
    hw_fetch_vc(_sdl3_mixer SDL_mixer SDL3_mixer ${HW_SDL3_MIXER_VERSION})
    find_package(SDL3       REQUIRED CONFIG PATHS ${_sdl3}/cmake       NO_DEFAULT_PATH)
    find_package(SDL3_image REQUIRED CONFIG PATHS ${_sdl3_image}/cmake NO_DEFAULT_PATH)
    find_package(SDL3_mixer REQUIRED CONFIG PATHS ${_sdl3_mixer}/cmake NO_DEFAULT_PATH)
    # SDL3_image/SDL3_mixer load these codec DLLs at runtime (PNG, OGG/Opus, XMP, ...).
    file(GLOB _hw_runtime_dlls
        ${_sdl3}/lib/x64/*.dll
        ${_sdl3_image}/lib/x64/*.dll ${_sdl3_image}/lib/x64/optional/*.dll
        ${_sdl3_mixer}/lib/x64/*.dll ${_sdl3_mixer}/lib/x64/optional/*.dll)

    # libopenmpt (plays the MO3 music): official prebuilt Windows dev package.
    set(_mpt ${_deps}/libopenmpt-${HW_OPENMPT_VERSION})
    if(NOT EXISTS ${_mpt}/inc/libopenmpt/libopenmpt.h)
        set(_zip ${_deps}/libopenmpt-${HW_OPENMPT_VERSION}-dev.zip)
        message(STATUS "Downloading libopenmpt ${HW_OPENMPT_VERSION}")
        file(DOWNLOAD
             https://lib.openmpt.org/files/libopenmpt/dev/libopenmpt-${HW_OPENMPT_VERSION}+release.dev.windows.vs2022.zip
             ${_zip} STATUS _st TLS_VERIFY ON)
        list(GET _st 0 _code)
        if(NOT _code EQUAL 0)
            file(REMOVE ${_zip})
            message(FATAL_ERROR "Failed to download libopenmpt ${HW_OPENMPT_VERSION}: ${_st}")
        endif()
        file(ARCHIVE_EXTRACT INPUT ${_zip} DESTINATION ${_mpt})
        file(REMOVE ${_zip})
    endif()
    add_library(hw_openmpt SHARED IMPORTED)
    set_target_properties(hw_openmpt PROPERTIES
        IMPORTED_IMPLIB ${_mpt}/lib/amd64/libopenmpt.lib
        IMPORTED_LOCATION ${_mpt}/bin/amd64/libopenmpt.dll
        INTERFACE_INCLUDE_DIRECTORIES ${_mpt}/inc
        INTERFACE_COMPILE_DEFINITIONS LIBOPENMPT_USE_DLL)
    file(GLOB _mpt_dlls ${_mpt}/bin/amd64/*.dll)
    list(APPEND _hw_runtime_dlls ${_mpt_dlls})
else()
    # Linux: system packages when they are new enough, otherwise (or with
    # HW_BUNDLE_SDL=ON) build them from the release sources as static libraries.
    option(HW_BUNDLE_SDL "Build SDL3/SDL3_image/SDL3_mixer from source instead of using system packages" OFF)
    if(NOT HW_BUNDLE_SDL)
        find_package(SDL3 3.2 CONFIG QUIET)
        find_package(SDL3_image 3.2 CONFIG QUIET)
        find_package(SDL3_mixer 3.0 CONFIG QUIET)
        if(NOT (SDL3_FOUND AND SDL3_image_FOUND AND SDL3_mixer_FOUND))
            message(STATUS "System SDL3/SDL3_image/SDL3_mixer not all found: building them from source")
            set(HW_BUNDLE_SDL ON)
        endif()
    endif()
    if(HW_BUNDLE_SDL)
        include(FetchContent)
        set(BUILD_SHARED_LIBS OFF)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        # Only what the game loads: PNG/JPG through stb_image, OGG/WAV through the built-in
        # decoders. No extra system codec libraries are needed.
        set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
        set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)   # PNG via stb too: no libpng/zlib needed
        foreach(_f AVIF BMP GIF JXL LBM PCX PNM QOI SVG TGA TIF WEBP XCF XPM XV ANI HDR)
            set(SDLIMAGE_${_f} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
        foreach(_f FLAC GME MIDI MOD MP3 OPUS WAVPACK AIFF VOC AU)
            set(SDLMIXER_${_f} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
        set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
        set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            URL ${_gh}/SDL/releases/download/release-${HW_SDL3_VERSION}/SDL3-${HW_SDL3_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_Declare(SDL3_image
            URL ${_gh}/SDL_image/releases/download/release-${HW_SDL3_IMAGE_VERSION}/SDL3_image-${HW_SDL3_IMAGE_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_Declare(SDL3_mixer
            URL ${_gh}/SDL_mixer/releases/download/release-${HW_SDL3_MIXER_VERSION}/SDL3_mixer-${HW_SDL3_MIXER_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_mixer)
    endif()

    # libopenmpt (plays the MO3 music): system package, else built from the release
    # sources as a static library with its bundled minimp3/stb_vorbis/miniz decoders.
    option(HW_BUNDLE_OPENMPT "Build libopenmpt from source instead of using the system package" OFF)
    if(NOT HW_BUNDLE_OPENMPT)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBOPENMPT QUIET IMPORTED_TARGET libopenmpt>=0.6)
        endif()
        if(LIBOPENMPT_FOUND)
            add_library(hw_openmpt INTERFACE)
            target_link_libraries(hw_openmpt INTERFACE PkgConfig::LIBOPENMPT)
        else()
            message(STATUS "System libopenmpt not found: building it from source")
            set(HW_BUNDLE_OPENMPT ON)
        endif()
    endif()
    if(HW_BUNDLE_OPENMPT)
        set(_m ${CMAKE_BINARY_DIR}/_deps/libopenmpt-${HW_OPENMPT_VERSION}+release)
        if(NOT EXISTS ${_m}/libopenmpt/libopenmpt.h)
            set(_tgz ${CMAKE_BINARY_DIR}/_deps/libopenmpt-${HW_OPENMPT_VERSION}.tar.gz)
            message(STATUS "Downloading libopenmpt ${HW_OPENMPT_VERSION} sources")
            file(DOWNLOAD
                 https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-${HW_OPENMPT_VERSION}+release.makefile.tar.gz
                 ${_tgz} STATUS _st TLS_VERIFY ON)
            list(GET _st 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE ${_tgz})
                message(FATAL_ERROR "Failed to download libopenmpt ${HW_OPENMPT_VERSION}: ${_st}")
            endif()
            file(ARCHIVE_EXTRACT INPUT ${_tgz} DESTINATION ${CMAKE_BINARY_DIR}/_deps)
            file(REMOVE ${_tgz})
        endif()
        file(GLOB _mpt_src
            ${_m}/src/openmpt/all/*.cpp ${_m}/src/openmpt/base/*.cpp
            ${_m}/src/openmpt/logging/*.cpp ${_m}/src/openmpt/random/*.cpp
            ${_m}/common/*.cpp
            ${_m}/src/openmpt/fileformat_base/*.cpp ${_m}/src/openmpt/soundbase/*.cpp
            ${_m}/src/openmpt/soundfile_data/*.cpp
            ${_m}/soundlib/*.cpp ${_m}/soundlib/plugins/*.cpp ${_m}/soundlib/plugins/dmo/*.cpp
            ${_m}/sounddsp/*.cpp ${_m}/libopenmpt/*.cpp)
        add_library(hw_openmpt STATIC ${_mpt_src}
            ${_m}/include/minimp3/minimp3.c ${_m}/include/stb_vorbis/stb_vorbis.c ${_m}/include/miniz/miniz.c)
        target_compile_features(hw_openmpt PRIVATE cxx_std_17)
        target_compile_definitions(hw_openmpt PRIVATE
            LIBOPENMPT_BUILD MPT_WITH_MINIZ MPT_WITH_MINIMP3
            MPT_WITH_STBVORBIS STB_VORBIS_NO_PULLDATA_API STB_VORBIS_NO_STDIO)
        target_include_directories(hw_openmpt PRIVATE ${_m} ${_m}/src ${_m}/common ${_m}/include)
        target_include_directories(hw_openmpt INTERFACE ${_m})
        set_target_properties(hw_openmpt PROPERTIES POSITION_INDEPENDENT_CODE ON)
        # SDL3_mixer built from source links its own stb_vorbis too: give libopenmpt's copy
        # (and libopenmpt's calls into it) different names so the two don't clash.
        file(READ ${_m}/include/stb_vorbis/stb_vorbis.c _stbv)
        string(REGEX MATCHALL "stb_vorbis_[A-Za-z0-9_]+" _stbv_syms "${_stbv}")
        list(REMOVE_DUPLICATES _stbv_syms)
        set(_rename "/* Generated by CMakeLists.txt: libopenmpt's private stb_vorbis names. */\n")
        foreach(_s IN LISTS _stbv_syms)
            string(APPEND _rename "#define ${_s} hw_openmpt_${_s}\n")
        endforeach()
        file(WRITE ${CMAKE_BINARY_DIR}/hw_openmpt_stb_vorbis.h "${_rename}")
        target_compile_options(hw_openmpt PRIVATE "SHELL:-include ${CMAKE_BINARY_DIR}/hw_openmpt_stb_vorbis.h")
    endif()
endif()

# ---------------------------------------------------------------------------
# Flash movies: the intro and the two story cutscenes (Images/intro.swf, middle.swf,
# final.swf). 3rdparty/flashrt is the native Flash player runtime from the Tanks recomp:
# its translator turns each movie's ActionScript into C++ at build time (from the user's
# own SWFs, never committed) and the runtime plays the timeline and rasterizes it.
# ---------------------------------------------------------------------------
set(_hw_swf_default "")
if(HW_DATA_DIR)
    set(_hw_swf_default "${HW_DATA_DIR}/Images")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Heavy Weapon Deluxe/Images/intro.swf")
    set(_hw_swf_default "${CMAKE_CURRENT_SOURCE_DIR}/Heavy Weapon Deluxe/Images")
endif()
set(HW_FLASH_SWF_DIR "${_hw_swf_default}" CACHE PATH
    "Folder with the game's intro.swf / middle.swf / final.swf, translated at build time")
find_package(Python3 COMPONENTS Interpreter)
set(_fr ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/flashrt)

file(GLOB _plutovg_src ${_fr}/plutovg/source/*.c)
add_library(hw_plutovg STATIC ${_plutovg_src})
target_include_directories(hw_plutovg PUBLIC ${_fr}/plutovg/include)
target_compile_definitions(hw_plutovg PUBLIC PLUTOVG_BUILD_STATIC)
set_target_properties(hw_plutovg PROPERTIES C_STANDARD 11)
if(MSVC)
    # The recomp's PlutoVG fork and raster.cpp use GCC's __thread for per-thread state.
    target_compile_definitions(hw_plutovg PUBLIC "__thread=__declspec(thread)")
endif()

add_library(hw_flashrt STATIC
    ${_fr}/rt/core.cpp ${_fr}/rt/builtins.cpp ${_fr}/rt/display.cpp ${_fr}/rt/swfload.cpp
    ${_fr}/rt/audio.cpp ${_fr}/rt/raster.cpp ${_fr}/rt/snapshot.cpp ${_fr}/rt/platform.cpp)
target_include_directories(hw_flashrt PUBLIC ${_fr}/rt PRIVATE ${_fr})
target_compile_features(hw_flashrt PUBLIC cxx_std_20)
target_link_libraries(hw_flashrt PUBLIC hw_plutovg SDL3::SDL3)
foreach(_t hw_plutovg hw_flashrt)
    if(MSVC)
        target_compile_options(${_t} PRIVATE /W0 /utf-8)
    else()
        target_compile_options(${_t} PRIVATE -w -fno-strict-aliasing)
    endif()
endforeach()

set(_hw_movies "")
foreach(_mv intro middle final)
    set(_swf "${HW_FLASH_SWF_DIR}/${_mv}.swf")
    set(_gen "${CMAKE_BINARY_DIR}/flashgen/${_mv}")
    if(HW_FLASH_SWF_DIR AND EXISTS "${_swf}" AND Python3_FOUND)
        file(GLOB _tools ${_fr}/tools/*.py)
        add_custom_command(
            OUTPUT "${_gen}/gen.stamp" "${_gen}/gen_atoms.cpp" "${_gen}/gen_tables.cpp"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${_gen}"
            COMMAND Python3::Interpreter -I -B "${_fr}/tools/gen.py" "${_swf}" "${_gen}" --ns mv_${_mv} --title ${_mv}
            COMMAND ${CMAKE_COMMAND} -E touch "${_gen}/gen.stamp"
            DEPENDS "${_swf}" ${_tools}
            COMMENT "Translating the ActionScript of ${_mv}.swf to C++")
        # The number of gen_N.cpp files depends on the movie: compile them through unity files.
        set(_units "")
        foreach(_i RANGE 0 3)
            set(_u "${CMAKE_BINARY_DIR}/flashgen/${_mv}_unit_${_i}.cpp")
            file(WRITE "${_u}.in" "#if __has_include(\"gen_${_i}.cpp\")\n#include \"gen_${_i}.cpp\"\n#endif\n")
            configure_file("${_u}.in" "${_u}" COPYONLY)
            set_source_files_properties("${_u}" PROPERTIES OBJECT_DEPENDS "${_gen}/gen.stamp")
            list(APPEND _units "${_u}")
        endforeach()
        add_library(hw_movie_${_mv} STATIC ${_units} "${_gen}/gen_atoms.cpp" "${_gen}/gen_tables.cpp")
        target_include_directories(hw_movie_${_mv} PRIVATE "${_gen}")
        message(STATUS "Flash movie ${_mv}: translating ${_swf}")
    else()
        # No SWF at build time: an empty script table. The movie still plays (if the player has
        # the file at run time), just without its timeline scripts.
        set(_stub "${CMAKE_BINARY_DIR}/flashgen/${_mv}_stub.cpp")
        file(WRITE "${_stub}"
            "#include \"genrt.h\"\nnamespace fl {\nstatic void mv_${_mv}_noAtoms() {}\n"
            "extern const GameCode g_code_mv_${_mv} = {\"mv_${_mv}\", \"${_mv}\", nullptr, 0, nullptr, 0, 640, 480, 12.0f, mv_${_mv}_noAtoms, nullptr, 0};\n}\n")
        add_library(hw_movie_${_mv} STATIC "${_stub}")
        message(STATUS "Flash movie ${_mv}: ${_mv}.swf not found at build time (HW_FLASH_SWF_DIR), its scripts are left out")
    endif()
    target_link_libraries(hw_movie_${_mv} PRIVATE hw_flashrt)
    if(MSVC)
        target_compile_options(hw_movie_${_mv} PRIVATE /W0 /utf-8 /bigobj)
    else()
        target_compile_options(hw_movie_${_mv} PRIVATE -w -fno-strict-aliasing -O1)
    endif()
    list(APPEND _hw_movies hw_movie_${_mv})
endforeach()

# ---------------------------------------------------------------------------
# Game
# ---------------------------------------------------------------------------
add_executable(heavyweapon WIN32 ${SOURCES})
target_include_directories(heavyweapon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/tinyxml2)
target_link_libraries(heavyweapon PRIVATE
    SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer SDL3::SDL3 hw_openmpt hw_lua
    hw_flashrt ${_hw_movies} OpenGL::GL)
if(HW_PERF)
    target_compile_definitions(heavyweapon PRIVATE HW_PERF)
endif()
if(HW_DATA_DIR)
    file(TO_CMAKE_PATH "${HW_DATA_DIR}" _hw_data_dir)
    target_compile_definitions(heavyweapon PRIVATE "HW_DATA_DIR=\"${_hw_data_dir}\"")
endif()

if(WIN32)
    target_compile_definitions(heavyweapon PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    target_compile_options(heavyweapon PRIVATE /W3 /utf-8 /MP)
    set_property(TARGET heavyweapon PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:heavyweapon>)
    add_custom_command(TARGET heavyweapon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_hw_runtime_dlls} $<TARGET_FILE_DIR:heavyweapon>
        VERBATIM)
    install(TARGETS heavyweapon RUNTIME DESTINATION .)
    install(FILES ${_hw_runtime_dlls} DESTINATION .)
else()
    target_compile_definitions(heavyweapon PRIVATE GL_GLEXT_PROTOTYPES)
    target_compile_options(heavyweapon PRIVATE -Wall -Wextra)
    install(TARGETS heavyweapon RUNTIME DESTINATION bin)
endif()
