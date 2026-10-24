cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

if(NOT DEFINED CMAKE_COMPILE_WARNING_AS_ERROR)
  set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
endif()

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" ON)

if(NOT ENABLE_QT)
  message(FATAL_ERROR "obs-ssp requires Qt for camera control and discovery.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core Network)

  # Qt's macOS package still links the legacy AGL framework, which was removed from the macOS 26 SDK.
  if(OS_MACOS AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_wrap_opengl_libraries WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wrap_opengl_libraries)
      list(FILTER _wrap_opengl_libraries EXCLUDE REGEX "(^|/)AGL\\.framework$|^-framework AGL$")
      set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libraries}")
    endif()
  endif()

  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOMOC_COMPILER_PREDEFINES OFF AUTOUIC ON AUTORCC ON
  )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/Modules")

set(
  obs-ssp_SOURCES
  src/obs-ssp.cpp
  src/obs-ssp-source.cpp
  src/ffmpeg-decode.c
  src/controller/cameraconfig.cpp
  src/controller/cameracontroller.cpp
  src/ssp-mdns.cpp
  src/ssp-controller.cpp
  src/VFrameQueue.cpp
  src/ssp-client-iso.cpp
  src/util/pipe.c
)

if(OS_WINDOWS)
  list(APPEND obs-ssp_SOURCES src/util/pipe-windows.c)
else()
  list(APPEND obs-ssp_SOURCES src/util/pipe-posix.c)
endif()

set(
  obs-ssp_HEADERS
  src/obs-ssp.h
  src/ssp-mdns.h
  src/ssp-controller.h
  src/VFrameQueue.h
  src/ssp-client-iso.h
  src/util/pipe.h
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${obs-ssp_SOURCES} ${obs-ssp_HEADERS})

# Keep legacy decoder warnings visible without requiring changes to the excluded source.
set_source_files_properties(
  src/ffmpeg-decode.c
  PROPERTIES
    COMPILE_OPTIONS
      "$<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-error=switch>;$<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-error=newline-eof>"
)

# /!\ TAKE NOTE: No need to edit things past this point /!\

# --- Platform-independent build settings ---
# Qt adds its own FindFFmpeg; use the component targets that keep SDK headers and libraries together.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/Modules")
find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil)
add_subdirectory(thirdpty)

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/lib/ssp/include ${CMAKE_SOURCE_DIR}/ssp_connector
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_HEVC)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE FFmpeg::avcodec FFmpeg::avutil mdns)

option(SSP_BUILD_TESTS "Build SSP protocol unit tests" OFF)
if(SSP_BUILD_TESTS)
  enable_testing()
endif()
add_subdirectory(ssp_connector)
if(USE_NATIVE_SSP OR OS_LINUX)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SSP_NATIVE_CONNECTOR)
endif()
add_dependencies(${CMAKE_PROJECT_NAME} ssp-connector)

if(OS_MACOS)
  install(TARGETS ssp-connector DESTINATION "./${CMAKE_PROJECT_NAME}.plugin/Contents/MacOS")
  if(LIBSSP_LIBRARY)
    install(FILES ${LIBSSP_LIBRARY} DESTINATION "./${CMAKE_PROJECT_NAME}.plugin/Contents/Frameworks")
  endif()
elseif(OS_WINDOWS)
  install(TARGETS ssp-connector DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
  if(LIBSSP_LIBRARY)
    install(FILES ${LIBSSP_LIBRARY} DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
  endif()
elseif(OS_LINUX)
  install(TARGETS ssp-connector DESTINATION ${CMAKE_INSTALL_BINDIR})
  if(LIBSSP_LIBRARY)
    install(FILES ${LIBSSP_LIBRARY} DESTINATION ${CMAKE_INSTALL_LIBDIR})
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(SSP_BUILD_TESTS)
  add_executable(test-camera-imaging cmake/tests/test-camera-imaging.cpp)
  target_include_directories(test-camera-imaging PRIVATE src)
  target_link_libraries(test-camera-imaging PRIVATE Qt6::Core Qt6::Network)
  add_test(NAME camera-imaging COMMAND test-camera-imaging)

  add_executable(test-frame-queue cmake/tests/test-frame-queue.cpp src/VFrameQueue.cpp)
  target_include_directories(test-frame-queue PRIVATE src lib/ssp/include)
  target_link_libraries(test-frame-queue PRIVATE Qt6::Core OBS::libobs)
  add_test(NAME frame-queue COMMAND test-frame-queue)

  add_executable(
    test-camera-live
    cmake/tests/test-camera-live.cpp
    src/ssp-controller.cpp
    src/controller/cameracontroller.cpp
    src/controller/cameraconfig.cpp
  )
  set_target_properties(test-camera-live PROPERTIES AUTOMOC ON)
  target_include_directories(test-camera-live PRIVATE src)
  target_link_libraries(test-camera-live PRIVATE Qt6::Core Qt6::Network OBS::libobs)

  add_executable(test-camera-config cmake/tests/test-camera-config.cpp src/controller/cameraconfig.cpp)
  target_include_directories(test-camera-config PRIVATE src)
  target_link_libraries(test-camera-config PRIVATE Qt6::Core Qt6::Network)
  add_test(NAME camera-config COMMAND test-camera-config)

  add_executable(test-camera-settings cmake/tests/test-camera-settings.cpp)
  target_include_directories(test-camera-settings PRIVATE src)
  target_link_libraries(test-camera-settings PRIVATE Qt6::Core)
  add_test(NAME camera-settings COMMAND test-camera-settings)

  add_executable(test-decoder cmake/tests/test-decoder.c src/ffmpeg-decode.c)
  target_include_directories(test-decoder PRIVATE src)
  target_link_libraries(test-decoder PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avutil)
  target_compile_definitions(test-decoder PRIVATE ENABLE_HEVC)
  add_executable(test-decoder-prores cmake/tests/test-decoder-prores.c src/ffmpeg-decode.c)
  target_include_directories(test-decoder-prores PRIVATE src)
  target_link_libraries(test-decoder-prores PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avutil)
  target_compile_definitions(test-decoder-prores PRIVATE ENABLE_HEVC)
  add_executable(test-decoder-video cmake/tests/test-decoder-video.c src/ffmpeg-decode.c)
  add_executable(test-decoder-pcm cmake/tests/test-decoder-pcm.c src/ffmpeg-decode.c)
  foreach(_decoder_target test-decoder-video test-decoder-pcm)
    target_include_directories(${_decoder_target} PRIVATE src)
    target_link_libraries(${_decoder_target} PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avutil)
    target_compile_definitions(${_decoder_target} PRIVATE ENABLE_HEVC)
  endforeach()
  set(SSP_TEST_OLD_FFMPEG_INCLUDE "" CACHE PATH "Optional actual incompatible FFmpeg headers for ABI rejection test")
  if(SSP_TEST_OLD_FFMPEG_INCLUDE)
    add_executable(test-decoder-incompatible cmake/tests/test-decoder-incompatible.c src/ffmpeg-decode.c)
    target_include_directories(test-decoder-incompatible BEFORE PRIVATE ${SSP_TEST_OLD_FFMPEG_INCLUDE} src)
    target_link_libraries(test-decoder-incompatible PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avutil)
    target_compile_definitions(test-decoder-incompatible PRIVATE ENABLE_HEVC)
    add_test(NAME decoder-incompatible COMMAND test-decoder-incompatible)
  endif()
  find_program(FFMPEG_EXECUTABLE ffmpeg)
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(FFMPEG_EXECUTABLE AND Python3_Interpreter_FOUND)
    add_test(
      NAME decoder-aac
      COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/test_decode.py $<TARGET_FILE:test-decoder>
        ${FFMPEG_EXECUTABLE}
    )
  endif()
  if(FFMPEG_EXECUTABLE AND Python3_Interpreter_FOUND)
    add_test(
      NAME decoder-matrix
      COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/test_decode_matrix.py ${FFMPEG_EXECUTABLE}
        $<TARGET_FILE:test-decoder-video> $<TARGET_FILE:test-decoder-pcm>
    )
    set_tests_properties(decoder-matrix PROPERTIES TIMEOUT 180)
  endif()
  if(FFMPEG_EXECUTABLE AND Python3_Interpreter_FOUND)
    add_test(
      NAME decoder-prores
      COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/test_decode_prores.py ${FFMPEG_EXECUTABLE}
        $<TARGET_FILE:test-decoder-prores>
    )
  endif()
  if(OS_MACOS)
    set(_test_rpaths "")
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
      list(APPEND _test_rpaths "${_prefix}" "${_prefix}/lib" "${_prefix}/Frameworks")
    endforeach()
    list(JOIN _test_rpaths " " _test_rpath_string)
    foreach(
      _test_target
      test-camera-imaging
      test-camera-settings
      test-camera-config
      test-camera-live
      test-frame-queue
      test-decoder
      test-decoder-prores
      test-decoder-video
      test-decoder-pcm
    )
      set_target_properties(
        ${_test_target}
        PROPERTIES
          BUILD_RPATH "${_test_rpaths}"
          BUILD_WITH_INSTALL_RPATH FALSE
          XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "${_test_rpath_string}"
          XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME NO
      )
    endforeach()
    if(TARGET test-decoder-incompatible)
      set_target_properties(
        test-decoder-incompatible
        PROPERTIES
          XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "${_test_rpath_string}"
          XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME NO
      )
    endif()
  endif()
endif()
