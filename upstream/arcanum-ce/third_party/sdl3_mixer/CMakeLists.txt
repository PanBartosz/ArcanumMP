include(FetchContent)

set(BUILD_SHARED_LIBS OFF)

set(SDLMIXER_AIFF OFF)
set(SDLMIXER_VOC OFF)
set(SDLMIXER_AU OFF)
set(SDLMIXER_FLAC_LIBFLAC OFF)
set(SDLMIXER_FLAC_DRFLAC OFF)
set(SDLMIXER_GME OFF)
set(SDLMIXER_MOD_XMP OFF)
set(SDLMIXER_MP3_MPG123 OFF)
set(SDLMIXER_MIDI_TIMIDITY OFF)
set(SDLMIXER_OPUS OFF)
set(SDLMIXER_VORBIS_STB OFF)
set(SDLMIXER_VORBIS_VORBISFILE OFF)
set(SDLMIXER_WAVPACK OFF)

FetchContent_Declare(SDL3_mixer
    GIT_REPOSITORY "https://github.com/libsdl-org/SDL_mixer"
    GIT_TAG "release-3.2.2"
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(SDL3_mixer)

# release-3.2.2 leaks the allocated converted string when an ID3v1 tag is
# empty. Retail ending narration contains such tags. Preserve metadata parsing
# and release that allocation in both branches of the existing check.
FetchContent_GetProperties(SDL3_mixer SOURCE_DIR _arcmp_mixer_source)
set(_arcmp_metadata_file "${_arcmp_mixer_source}/src/SDL_mixer_metadata_tags.c")
file(READ "${_arcmp_metadata_file}" _arcmp_metadata)
set(_arcmp_old "SDL_SetStringProperty(props, key, src_buf);\n            SDL_free(src_buf);\n        }")
set(_arcmp_new "SDL_SetStringProperty(props, key, src_buf);\n        }\n        SDL_free(src_buf);")
string(REPLACE "${_arcmp_old}" "${_arcmp_new}" _arcmp_metadata_fixed "${_arcmp_metadata}")
if(NOT _arcmp_metadata STREQUAL _arcmp_metadata_fixed)
    file(WRITE "${_arcmp_metadata_file}" "${_arcmp_metadata_fixed}")
endif()
