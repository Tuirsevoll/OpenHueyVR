cmake_minimum_required(VERSION 3.20)
project(hga VERSION 0.3.1 LANGUAGES C CXX)   # the version: shown with the debug mode (F2). CXX: whisper.cpp

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # gnu11: _Noreturn, __attribute__, M_PI
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    add_compile_options(/W3)
    add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# A release (-DHGA_RELEASE=ON) knows no paths of this machine: the scripts are found beside the
# executable (scripts/), the game data is the player's own disc, chosen at start-up.
option(HGA_RELEASE "build for distribution (no built-in paths; Windows: no console)" OFF)
# the build's name, which the updater compares with the newest release's (src/platform/update.c):
# a release's is the version and its commit (tools/hga_release.sh: "0.1.1-c6b7113")
if(NOT HGA_BUILD)
    set(HGA_BUILD "${PROJECT_VERSION}")
endif()
get_filename_component(SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts" ABSOLUTE)
get_filename_component(DEFAULT_DATA "${CMAKE_CURRENT_SOURCE_DIR}/../../Haunting Ground (USA)/data" ABSOLUTE)
set(DEFAULT_EXE "${CMAKE_CURRENT_SOURCE_DIR}/../baserom/SLUS_210.75")
if(HGA_RELEASE)
    set(SCRIPT_DIR "")
    set(DEFAULT_DATA "")
    set(DEFAULT_EXE "")
endif()

# ---- the Forth system: ClankForth (external/clankforth, a submodule shared with OpenMizzurna - its
# language, prelude and tester built in; its own tests run with ours) ----
enable_testing()
set(CLANKFORTH_TESTS ON CACHE BOOL "the language's tests (ctest)")
# ClankForth is written for GCC/Clang - upstream's Windows release is cross-built with MinGW - so its
# sources use __attribute__ and __int128, which MSVC has no notion of. cmake/clankforth-msvc.patch
# guards the attributes and gives every 128-bit product and division an exact MSVC path (_mul128 /
# _umul128 and long division), and is applied here to the submodule's sources when it is missing:
# idempotent, so a `git submodule update` is undone by the next configure rather than by hand.
if(MSVC)
    find_package(Git QUIET)
    set(CLANKFORTH_MSVC_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clankforth-msvc.patch")
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}/external/clankforth" apply --check "${CLANKFORTH_MSVC_PATCH}"
            RESULT_VARIABLE CLANKFORTH_NEEDS_PATCH OUTPUT_QUIET ERROR_QUIET)
        if(CLANKFORTH_NEEDS_PATCH EQUAL 0)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}/external/clankforth" apply "${CLANKFORTH_MSVC_PATCH}"
                RESULT_VARIABLE CLANKFORTH_PATCHED OUTPUT_QUIET ERROR_QUIET)
            if(NOT CLANKFORTH_PATCHED EQUAL 0)
                message(FATAL_ERROR "ClankForth: could not apply cmake/clankforth-msvc.patch")
            endif()
            message(STATUS "ClankForth: applied the MSVC compatibility patch")
        endif()
    endif()
endif()
# (no TLS: the game gives its scripts no `net`, and OpenSSL would be one more library for the releases)
set(CLANKFORTH_TLS OFF CACHE BOOL "TLS on net's sockets (OpenHuey: none)" FORCE)
add_subdirectory(external/clankforth)
# MSVC's C runtime is one library: there is no libm to link, and ClankForth asks for it unconditionally.
# Dropped from both its own link line and the interface its tools inherit (a static library, so this is
# the only place `m` could break a link).
if(MSVC)
    foreach(_tgt clankforth)
        foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_libs ${_tgt} ${_prop})
            if(_libs)
                list(REMOVE_ITEM _libs m)
                set_target_properties(${_tgt} PROPERTIES ${_prop} "${_libs}")
            endif()
        endforeach()
    endforeach()
endif()

# The rest of ClankForth's POSIX: MSVC has no <dirent.h>, no clock_gettime, no nanosleep and no S_ISDIR.
# cmake/msvc holds them - the <dirent.h> found on this include path (system.c's dir-list), the rest
# included by the patch's forth_internal.h - so ClankForth 0.3 builds with MSVC as it does with GCC.
# PUBLIC, because a host that includes forth_internal.h itself (src/forth/bind_engine.c, and
# src/game/loot.c through its primitive macros) must find what that header includes.
if(MSVC)
    target_include_directories(clankforth PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc")
endif()

# Hewie's voice: whisper.cpp in-process (tiny or base). Tests, examples and GPU backends stay off.
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(external/whisper.cpp)
add_library(forth ALIAS clankforth)

# the game state the scripts share (no SDL: the forth tool and the tests have it too)
add_library(gamestate STATIC src/game/progress.c)
target_link_libraries(gamestate PUBLIC forth)

# the actor kernel (no SDL either)
add_library(hactor STATIC src/hactor/hactor.c)
target_link_libraries(hactor PUBLIC forth)

add_executable(forth-tool src/forth/repl.c)
set_target_properties(forth-tool PROPERTIES OUTPUT_NAME forth)
target_link_libraries(forth-tool PRIVATE forth gamestate hactor)
target_compile_definitions(forth-tool PRIVATE "HG2_SCRIPT_DIR=\"${SCRIPT_DIR}\"")

file(GLOB FORTH_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.fs")
foreach(t ${FORTH_TESTS})
    get_filename_component(name "${t}" NAME_WE)
    add_test(NAME ${name} COMMAND forth-tool "${t}")
endforeach()

# ---- the game ----
find_package(SDL3 REQUIRED CONFIG)

# ---- Steam Audio (3-D audio: HRTF binaural rendering) - an optional prebuilt SDK ------------
# Not a submodule: drop the official release zip's `steamaudio/` here (or point
# HGA_STEAMAUDIO_DIR at it): include/phonon.h, lib/windows-x64/phonon.lib, phonon.dll. Without
# it src/platform/saudio.c compiles to no-ops and the game is exactly as it was, so a missing SDK
# never blocks a build. docs/subsystems/audio3d.md and the README say how.
set(HGA_STEAMAUDIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/steamaudio" CACHE PATH
    "Steam Audio SDK: include/phonon.h, lib/windows-x64/phonon.lib and phonon.dll")
find_path(HGA_PHONON_INCLUDE NAMES phonon.h HINTS "${HGA_STEAMAUDIO_DIR}/include" NO_DEFAULT_PATH)
find_library(HGA_PHONON_LIB NAMES phonon HINTS "${HGA_STEAMAUDIO_DIR}/lib/windows-x64" NO_DEFAULT_PATH)
find_file(HGA_PHONON_DLL NAMES phonon.dll HINTS "${HGA_STEAMAUDIO_DIR}/lib/windows-x64" NO_DEFAULT_PATH)
if(HGA_PHONON_INCLUDE AND HGA_PHONON_LIB)
    message(STATUS "Steam Audio: ${HGA_PHONON_INCLUDE} (binaural 3-D audio on)")
    if(HGA_PHONON_DLL)
        add_library(hga_phonon SHARED IMPORTED)
        set_target_properties(hga_phonon PROPERTIES
            IMPORTED_LOCATION "${HGA_PHONON_DLL}"
            IMPORTED_IMPLIB "${HGA_PHONON_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${HGA_PHONON_INCLUDE}")
    else()
        add_library(hga_phonon UNKNOWN IMPORTED)
        set_target_properties(hga_phonon PROPERTIES
            IMPORTED_LOCATION "${HGA_PHONON_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${HGA_PHONON_INCLUDE}")
        message(WARNING "Steam Audio: phonon.dll not found beside phonon.lib: it must be copied beside openhuey.exe by hand")
    endif()
    set(HGA_HAVE_STEAMAUDIO ON)
else()
    message(STATUS "Steam Audio: not in ${HGA_STEAMAUDIO_DIR} (building without 3-D audio)")
endif()
# the game's data: its files (the disc, the library, the mods' layer), the formats and the
# library's conversions - the engine's and the checks' (tests/*_check.c)
add_library(hgadata STATIC
    src/core/files.c src/core/zip.c src/platform/disc.c src/platform/library.c
    src/data/pac.c src/data/roommesh.c src/data/gltfio.c src/data/roomgltf.c src/data/modelgltf.c src/data/propgltf.c src/data/roomtext.c src/data/tex.c src/data/texbank.c src/data/musicsrc.c
    src/data/flacenc.c src/data/msgtext.c src/data/model.c src/data/navmesh.c src/data/exe.c src/data/exetables.c
    src/data/soundbank.c src/data/vag.c src/data/samples.c src/data/sfd.c src/data/artpic.c
    src/third_party/stb.c src/third_party/dr_flac.c src/third_party/cgltf.c)
target_link_libraries(hgadata PUBLIC SDL3::SDL3)
if(NOT MSVC)
    target_link_libraries(hgadata PUBLIC m)
endif()

add_executable(hga
    src/main.c
    src/platform/gl.c src/platform/platform.c src/platform/vr.c src/platform/voice.c src/platform/update.c src/platform/sound.c src/platform/movie.c src/platform/music.c src/platform/seq.c src/platform/spu.c src/platform/snddrv.c src/platform/saudio.c
    src/render/render.c src/render/post.c
    src/game/room.c src/game/console.c src/game/actor.c src/game/doglegs.c src/game/humanlegs.c src/game/vrhands.c src/game/vrbody.c src/game/vrbones.c src/game/gripposes.c src/game/loot.c src/game/itemprops.c src/game/chair.c src/game/cloth.c src/game/world.c
    src/game/camdirector.c src/game/areas.c src/game/exits.c src/game/messages.c src/game/cutscene.c src/game/placed.c src/game/sprites.c src/game/mods.c src/game/particles.c src/game/things.c src/game/obstacles.c src/game/swimpath.c src/game/rumble.c src/game/props.c
    src/game/sound3d.c src/game/vrspots.c src/game/triggerprobe.c src/game/triggers.c src/game/vrmarkers.c
    src/game/vrspotprobe.c src/game/vrpath.c src/game/vrlevers.c src/game/vrmirror.c
    src/forth/bind_engine.c)
target_link_libraries(hga PRIVATE hgadata forth gamestate hactor SDL3::SDL3 whisper)
if(HGA_HAVE_STEAMAUDIO)
    target_link_libraries(hga PRIVATE hga_phonon)
    target_compile_definitions(hga PRIVATE HGA_STEAMAUDIO)
endif()
if(WIN32)
    target_link_libraries(hga PRIVATE ole32 uuid)
endif()
target_include_directories(hga PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/openxr/include")
if(WIN32)
    add_custom_command(TARGET hga POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/external/openxr/openxr_loader.dll"
            "$<TARGET_FILE_DIR:hga>/openxr_loader.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:whisper>" "$<TARGET_FILE:ggml>" "$<TARGET_FILE:ggml-base>" "$<TARGET_FILE:ggml-cpu>"
            "$<TARGET_FILE_DIR:hga>")
endif()
set_target_properties(hga PROPERTIES OUTPUT_NAME openhuey)   # (the target keeps its old name: the tests' COMMAND hga)   # (GL through SDL_GL_GetProcAddress: platform/gl.c)
target_compile_definitions(hga PRIVATE "HGA_VERSION=\"${PROJECT_VERSION}\"" "HGA_BUILD=\"${HGA_BUILD}\"" "HG2_SCRIPT_DIR=\"${SCRIPT_DIR}\"" "HG2_DEFAULT_DATA=\"${DEFAULT_DATA}\""
    "HG2_DEFAULT_EXE=\"${DEFAULT_EXE}\"")
if(HGA_RELEASE)
    target_compile_definitions(hga PRIVATE HGA_RELEASE HGA_UPDATER)   # (a release updates itself: --update, the title's UPDATE)
    if(WIN32)
        set_target_properties(hga PROPERTIES WIN32_EXECUTABLE ON)   # (SDL3 gives the WinMain)
        if(MSVC)
            # the MinGW SDL3 package upstream releases with carries SDL3main (the WinMain that calls
            # main); vcpkg's MSVC SDL3 does not, so the no-console build names the CRT's own entry instead.
            set_target_properties(hga PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
        endif()
    else()
        set_target_properties(hga PROPERTIES BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
    endif()
endif()

# the subsystems' tests: headless in the game (they need the game's data), tests/<subsystem>/test_*.fs.
# `ctest` is a quick run: each file to its `full-suite` (the tests checking state after words run),
# the long conversion checks skipped; `OPENHUEY_FULL_TESTS=1 ctest` the full one - every test, all through
file(GLOB GAME_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*/test_*.fs")
foreach(t ${GAME_TESTS})
    get_filename_component(name "${t}" NAME_WE)
    get_filename_component(dir "${t}" DIRECTORY)
    get_filename_component(sub "${dir}" NAME)
    add_test(NAME ${sub}_${name} COMMAND hga --test "${t}")
endforeach()

# the textures' conversion: every bank of the developer's data converted and read back, the same pixels
add_executable(texbank-check tests/texbank_check.c)
target_link_libraries(texbank-check PRIVATE hgadata)
add_test(NAME texbank_check COMMAND texbank-check "${DEFAULT_DATA}" "${CMAKE_CURRENT_BINARY_DIR}/texbank-check.scratch")
set_tests_properties(texbank_check PROPERTIES SKIP_RETURN_CODE 77)

# the sample mod, turned on: tests/mods/*.fs (not test_*.fs - the rest run without mods)
add_test(NAME mods_room_tally COMMAND hga --mod "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/room-tally"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/room-tally.fs")

# the items lying about shown as props (mod API 1.5)
add_test(NAME mods_item_props COMMAND hga --mod "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/item-props"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/item-props.fs")

# a mod whose script never ends as it loads: stopped (the load's step limit), off, the game on
add_test(NAME mods_endless COMMAND hga --mod "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/endless-mod"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/endless.fs")

# mods' manifests and files: the order, what turns a mod off, the files layer, a mod's textures
add_executable(mods-check tests/mods_check.c src/game/mods.c)
target_link_libraries(mods-check PRIVATE hgadata forth)
add_test(NAME mods_check COMMAND mods-check "${DEFAULT_DATA}" "${CMAKE_CURRENT_BINARY_DIR}/mods-check.scratch")
set_tests_properties(mods_check PROPERTIES SKIP_RETURN_CODE 77)
# ... and zipped (the folder inside the zip, as zipping a folder makes it)
add_test(NAME mods_zip_sample COMMAND ${CMAKE_COMMAND} -E tar cf "${CMAKE_CURRENT_BINARY_DIR}/room-tally.zip" --format=zip room-tally
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods")
set_tests_properties(mods_zip_sample PROPERTIES FIXTURES_SETUP room_tally_zip)
add_test(NAME mods_room_tally_zipped COMMAND hga --mod "${CMAKE_CURRENT_BINARY_DIR}/room-tally.zip"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/room-tally.fs")
set_tests_properties(mods_room_tally_zipped PROPERTIES FIXTURES_REQUIRED room_tally_zip)

# props (mod API 1.5): any glTF - a .gltf with its files, data: URIs, a .glb - read as authored
add_executable(prop-check tests/prop_check.c)
target_link_libraries(prop-check PRIVATE hgadata)
add_test(NAME prop_check COMMAND prop-check)

# the music's conversion: every ADX made a FLAC and both played, looped too - the same samples
add_executable(music-check tests/music_check.c)
target_link_libraries(music-check PRIVATE hgadata)
add_test(NAME music_check COMMAND music-check "${DEFAULT_DATA}")
set_tests_properties(music_check PROPERTIES SKIP_RETURN_CODE 77)

# the codecs: optimized in every build (the import's encoding, the checks, the music's decoding)
if(MSVC)
    # Debug already passes /RTC1, which cannot be combined with /O2. Release is optimized by the config.
    set_source_files_properties(src/data/flacenc.c src/data/musicsrc.c src/third_party/dr_flac.c src/third_party/stb.c
        PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/O2>")
else()
    set_source_files_properties(src/data/flacenc.c src/data/musicsrc.c src/third_party/dr_flac.c src/third_party/stb.c
        PROPERTIES COMPILE_OPTIONS "-O2")
endif()

# the messages' conversion: every table written as text and read back - the same messages
add_executable(messages-check tests/messages_check.c)
target_link_libraries(messages-check PRIVATE hgadata)
add_test(NAME messages_check COMMAND messages-check "${DEFAULT_DATA}")
set_tests_properties(messages_check PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME mods_flower_scent COMMAND hga --mod "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/flower-scent"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/flower-scent.fs")

# the sound banks' conversion: every sample made a WAV and both played as the SPU plays them
add_executable(sound-check tests/sound_check.c)
target_link_libraries(sound-check PRIVATE hgadata)
add_test(NAME sound_check COMMAND sound-check "${DEFAULT_DATA}")
set_tests_properties(sound_check PROPERTIES SKIP_RETURN_CODE 77)

# the SPU's voices: a sample set on a playing voice waits for its key on (a kept loop plays on)
add_executable(spu-check tests/spu_check.c src/platform/spu.c src/platform/saudio.c)
target_link_libraries(spu-check PRIVATE hgadata)
if(HGA_HAVE_STEAMAUDIO)
    target_compile_definitions(spu-check PRIVATE HGA_STEAMAUDIO)
    target_link_libraries(spu-check PRIVATE hga_phonon)
endif()
add_test(NAME spu_check COMMAND spu-check)

# the hands' wall sweep: a hand's path across a room's wall is found, a floor's isn't (detection only).
# vrhands.c's grabs call the hiding spots' and levers' modules, so they come along (their own readers
# and the code that names a room); placed.c stays out - this check stubs the parts of it it needs.
add_executable(hands-sweep-check tests/hands_sweep_check.c src/game/vrhands.c src/game/vrbody.c src/game/vrbones.c src/game/gripposes.c src/game/itemprops.c
    src/game/vrspots.c src/game/vrlevers.c src/game/vrmirror.c src/game/vrmarkers.c src/game/vrpath.c src/game/triggers.c)
target_link_libraries(hands-sweep-check PRIVATE hgadata forth)
add_test(NAME hands_sweep_check COMMAND hands-sweep-check "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/fiona-vr-rig/library")

# Fiona's VR body: the bones by name, the ratios, the feet kept, the helpers and the head (vrbody.c)
add_executable(vrbody-check tests/vrbody_check.c src/game/vrbody.c src/game/vrbones.c)
target_link_libraries(vrbody-check PRIVATE hgadata)
add_test(NAME vrbody_check COMMAND vrbody-check "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/fiona-vr-rig/library")

# the VR bones by outfit: each costume's vr_bones.txt, resolved against its own skeleton (vrbones.c)
add_executable(vr-bones-check tests/vr_bones_check.c src/game/vrbones.c)
target_link_libraries(vr-bones-check PRIVATE hgadata)
add_test(NAME vr_bones_check COMMAND vr-bones-check "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/fiona-vr-rig/library")

# a thing in a hand: drawn between the last two ticks' hand frames, as her eyes are (loot.c)
add_executable(loot-frame-check tests/loot_frame_check.c src/game/loot.c src/game/itemprops.c src/game/props.c src/game/gripposes.c src/game/vrbones.c)
target_link_libraries(loot-frame-check PRIVATE hgadata forth)
target_compile_definitions(loot-frame-check PRIVATE "HG2_SCRIPT_DIR=\"${SCRIPT_DIR}\"")
target_compile_definitions(loot-frame-check PRIVATE "ITEMPROPS_SAMPLE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/item-props/library\"")
add_test(NAME loot_frame_check COMMAND loot-frame-check)

# the held items' grips: PROPS/grips.txt read as the held models' data, and its wrist matrix
add_executable(itemprops-check tests/itemprops_check.c src/game/itemprops.c src/game/gripposes.c src/game/vrbones.c)
target_link_libraries(itemprops-check PRIVATE hgadata)
target_compile_definitions(itemprops-check PRIVATE "ITEMPROPS_SAMPLE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/item-props/library\"")
add_test(NAME itemprops_check COMMAND itemprops-check)

# the mods' finger grip poses: PROPS/grip_poses.txt parsed, the left mirror, slerp, the rig check
add_executable(gripposes-check tests/gripposes_check.c src/game/gripposes.c src/game/vrbones.c)
target_link_libraries(gripposes-check PRIVATE hgadata)
target_compile_definitions(gripposes-check PRIVATE
    "GRIPPOSES_POSE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/item-props/library\""
    "GRIPPOSES_MODEL_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/fiona-vr-rig/library\"")
add_test(NAME gripposes_check COMMAND gripposes-check)

# the VR markers: PROPS/hidespots.txt's spot boxes and PROPS/levers.txt's - parsed, their test,
# and the way in and out of a box's own space (vrmarkers.c; triggers.c gives the room's name)
add_executable(vrmarkers-check tests/vrmarkers_check.c src/game/vrmarkers.c src/game/triggers.c)
target_link_libraries(vrmarkers-check PRIVATE hgadata forth)
target_compile_definitions(vrmarkers-check PRIVATE
    "ITEMPROPS_SAMPLE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/item-props/library\"")
add_test(NAME vrmarkers_check COMMAND vrmarkers-check)

# the hiding spots' geometry with no room behind it: the handle path a hand's target is projected on
# (vrpath.c) and a track's pose at a fractional frame (placed.c's placed_pose_at)
add_executable(vrspots-check tests/vrspots_check.c src/game/vrpath.c src/game/placed.c)
target_link_libraries(vrspots-check PRIVATE hgadata)
add_test(NAME vrspots_check COMMAND vrspots-check)

# the curtained mirrors a hand draws: a fire once per crossing, a let-go springing back to its
# flag's end, and a hiding spot's curtain never registered (vrmirror.c; the morph decode, placed.c)
add_executable(vrmirror-check tests/vrmirror_check.c src/game/vrmirror.c src/game/vrpath.c src/game/placed.c)
target_link_libraries(vrmirror-check PRIVATE hgadata forth)
add_test(NAME vrmirror_check COMMAND vrmirror-check)

# the movies' conversion: every SFD split as the import splits it - the same video packets, the same sound
add_executable(movie-check tests/movie_check.c)
target_link_libraries(movie-check PRIVATE hgadata)
add_test(NAME movie_check COMMAND movie-check "${DEFAULT_DATA}")
set_tests_properties(movie_check PROPERTIES SKIP_RETURN_CODE 77)

# the rooms' conversion: each PAC less its converted sections - the rest the same bytes and sizes
add_executable(room-check tests/room_check.c)
target_link_libraries(room-check PRIVATE hgadata)
add_test(NAME room_check COMMAND room-check "${DEFAULT_DATA}")
set_tests_properties(room_check PROPERTIES SKIP_RETURN_CODE 77)

# the models' conversion: every .PCK as glTF (+ its motion bank) and read back - the same Model
add_executable(model-check tests/model_check.c)
target_link_libraries(model-check PRIVATE hgadata)
add_test(NAME model_check COMMAND model-check "${DEFAULT_DATA}")
set_tests_properties(model_check PROPERTIES SKIP_RETURN_CODE 77)
# the mods menu: a copy of the sample mods as the mods folder - driven, saved, then read again
add_test(NAME mods_menu_folder COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods"
    "${CMAKE_CURRENT_BINARY_DIR}/mods-menu.scratch")
set_tests_properties(mods_menu_folder PROPERTIES FIXTURES_SETUP mods_menu_folder)
add_test(NAME mods_menu_clean COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/mods-menu.scratch/order.txt")
set_tests_properties(mods_menu_clean PROPERTIES FIXTURES_SETUP mods_menu_folder DEPENDS mods_menu_folder)
add_test(NAME mods_menu COMMAND hga --mods "${CMAKE_CURRENT_BINARY_DIR}/mods-menu.scratch"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/menu.fs")
set_tests_properties(mods_menu PROPERTIES FIXTURES_REQUIRED mods_menu_folder FIXTURES_SETUP mods_menu_saved)
add_test(NAME mods_menu_after COMMAND hga --mods "${CMAKE_CURRENT_BINARY_DIR}/mods-menu.scratch"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/menu-after.fs")
set_tests_properties(mods_menu_after PROPERTIES FIXTURES_REQUIRED "mods_menu_folder;mods_menu_saved")
# the title as a player meets it: --title brings it up headless too (not a test_*.fs: those run
# without it, straight into a new game)
add_test(NAME title_title COMMAND hga --title --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/title/title.fs")
# the languages (a European disc's library's): a mod gives the English data a little French
add_test(NAME title_languages COMMAND hga --title --mod "${CMAKE_CURRENT_SOURCE_DIR}/tests/title/languages-mod"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/title/languages.fs")
# back to the title from a game (pause.fs's quit, the game over, the ending all `soft-reset`): the
# game's particle systems must go with the sprites they draw into, or the title's next tick crashes
# (300 frames: a full run's - in a quick one it ends at once)
add_test(NAME title_back_to_title COMMAND hga --hidden --title --frames 300 --eval
    "variable n  : back  1 n +!  n @ 30 = if free-play then  n @ 200 = if soft-reset then ;  ' back on-tick  : quick  full-tests? 0= if  bye  then ;  quick")
set_tests_properties(title_back_to_title PROPERTIES ENVIRONMENT "OPENHUEY_HOME=${CMAKE_CURRENT_BINARY_DIR}/back-to-title.scratch")
# the updater: against releases made in the test (file://), and the title's UPDATE page
if(NOT WIN32)
    add_test(NAME update_run COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/update/run.sh" $<TARGET_FILE:hga> "${HGA_BUILD}"
        "${CMAKE_CURRENT_BINARY_DIR}")
endif()
string(REPLACE " " "%20" NO_RELEASE_URL "file://${CMAKE_CURRENT_BINARY_DIR}/no-release/")
add_test(NAME update_title COMMAND hga --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/update/title.fs")
set_tests_properties(update_title PROPERTIES ENVIRONMENT
    "OPENHUEY_UPDATE_URL=${NO_RELEASE_URL};OPENHUEY_UPDATE_DIR=${CMAKE_CURRENT_BINARY_DIR}/update-title.scratch")
add_test(NAME mods_first_person COMMAND hga --mod "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/first-person"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/first-person.fs")
add_test(NAME mods_free_play_entry COMMAND hga --mod "${CMAKE_CURRENT_SOURCE_DIR}/examples/mods/free-play-entry"
    --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/mods/free-play-entry.fs")


# ---- runtime DLLs beside the executables -----------------------------------------------------------
# A multi-config generator puts each target in build/<cfg>, while the DLLs it needs come from vcpkg's
# bin (SDL3) and from whisper's own targets. Copied beside every executable that needs them, so
# openhuey.exe, the -check tools and `ctest` start from anywhere: without this Windows fails them with
# STATUS_DLL_NOT_FOUND (exit code 0xc0000135), which shows up as a dialog that comes back for every run.
foreach(_tgt hga
             texbank-check mods-check prop-check music-check messages-check sound-check spu-check
             hands-sweep-check loot-frame-check movie-check room-check model-check vr-bones-check)
    if(TARGET ${_tgt})
        add_custom_command(TARGET ${_tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${_tgt}> "$<TARGET_FILE_DIR:${_tgt}>"
            COMMAND_EXPAND_LISTS
            COMMENT "Copying the runtime DLLs beside ${_tgt}")
    endif()
endforeach()