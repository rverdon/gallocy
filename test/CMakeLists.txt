set (GALLOCY_INCLUDE
  ../gallocy
  ../gallocy/heaplayers
  ../gallocy/external/sqlite3)
include_directories(${GALLOCY_INCLUDE})
include_directories(${gtest_SOURCE_DIR}/include)

set(test_sources
  gtest.cpp
  test_simple_malloc.cpp
  test_simple_free.cpp
  test_mmult.cpp
  test_hashmap.cpp
)
add_executable(gallocy_tests ${test_sources})
target_link_libraries(gallocy_tests gtest_main gallocy-core "dl")

install(TARGETS gallocy_tests DESTINATION bin)