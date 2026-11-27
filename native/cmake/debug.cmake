# Standalone debug transport. No FEX, Vulkan, Android UI or guest structs.
find_package(Threads REQUIRED)
add_library(hakux_debug STATIC
  "${NC_REPO}/native/debug/log_queue.c"
  "${NC_REPO}/native/debug/xbdm_protocol.c"
  "${NC_REPO}/native/debug/socket.c"
  "${NC_REPO}/native/debug/xbdm.c")
target_include_directories(hakux_debug PUBLIC "${NC_REPO}")
target_link_libraries(hakux_debug PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(hakux_debug PUBLIC ws2_32)
endif()
target_compile_options(hakux_debug PRIVATE -Wall -Wextra -Werror)
