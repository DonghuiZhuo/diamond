# ---------------------------------------------------------
# Directory Source files
# ---------------------------------------------------------

# Diamond data types
file(GLOB SRC_DIAMOND_DATA_TYPES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

set(SRC_DIAMOND_DATA_TYPES ${SRC_DIAMOND_DATA_TYPES} PARENT_SCOPE)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})