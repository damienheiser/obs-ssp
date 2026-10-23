if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mdns/CMakeLists.txt")
  message(FATAL_ERROR "Missing mDNS submodule. Run: git submodule update --init --recursive")
endif()

set(MDNS_BUILD_EXAMPLE Off)
add_subdirectory(mdns EXCLUDE_FROM_ALL)
