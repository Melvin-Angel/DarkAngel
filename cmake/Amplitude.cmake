# Later opt-in SDK import. The engine's M0 targets never include this file.
# Platform and SIMD definitions must match the tested static SDK, because the
# upstream import module does not attach them to its imported target.
function(darkangel_import_amplitude sdk_root)
  if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This Amplitude recipe is qualified only for Windows x64/MSVC")
  endif()
  set(AM_SDK_PATH "${sdk_root}")
  set(AM_SDK_PLATFORM "x64-windows")
  list(APPEND CMAKE_MODULE_PATH "${sdk_root}/cmake")
  find_package(AmplitudeAudioSDK REQUIRED)
  find_package(lz4 CONFIG REQUIRED)
  find_package(flatbuffers CONFIG REQUIRED)
  find_package(dylib CONFIG REQUIRED)
  if(NOT TARGET DarkAngelAmplitudeSDK)
    add_library(DarkAngelAmplitudeSDK INTERFACE)
    add_library(DarkAngel::AmplitudeSDK ALIAS DarkAngelAmplitudeSDK)
    target_link_libraries(DarkAngelAmplitudeSDK INTERFACE
      SparkyStudios::Audio::Amplitude::SDK::Static lz4::lz4 flatbuffers::flatbuffers dylib::dylib)
    target_compile_features(DarkAngelAmplitudeSDK INTERFACE cxx_std_20)
    target_compile_definitions(DarkAngelAmplitudeSDK INTERFACE
      AM_PLATFORM_WIN=1 AM_PLATFORM_APPLE=0 AM_PLATFORM_LINUX=0
      AM_PLATFORM_ANDROID=0 AM_PLATFORM_IOS=0 AM_PLATFORM_MACOS=0
      AM_PLATFORM_UNIX=0 AM_PLATFORM_EMSCRIPTEN=0
      AM_COMPILER_MSVC=1 AM_COMPILER_CLANG=0 AM_COMPILER_GCC=0
      AM_ARCH_X86_64=1 AM_ARCH_X86=0 AM_ARCH_ARM=0 AM_ARCH_ARM_64=0 AM_ARCH_ARM_V7=0
      AM_SDK_PLATFORM="x64-windows"
      AM_BUILDSYSTEM_ARCH_X86_SSE4_1 AM_BUILDSYSTEM_ARCH_X86_SSE2
      AM_BUILDSYSTEM_ARCH_X86_SSE3 AM_BUILDSYSTEM_ARCH_X86_SSSE3
      AM_BUILDSYSTEM_ARCH_X86_AVX AM_BUILDSYSTEM_ARCH_X86_AVX2
      AM_BUILDSYSTEM_ARCH_X86_POPCNT AM_BUILDSYSTEM_ARCH_X86_FMA3
      __SSE4_1__ __SSE2__ __SSE3__ __SSSE3__ __AVX__ __AVX2__ __FMA__)
    target_compile_options(DarkAngelAmplitudeSDK INTERFACE /arch:AVX2 /EHsc)
  endif()
endfunction()
