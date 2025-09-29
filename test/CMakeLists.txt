project(svn-lfs-exportTests LANGUAGES CXX)

find_package(Catch2 CONFIG REQUIRED)

add_executable(svn-lfs-export_test tests.cpp)
target_link_libraries(svn-lfs-export_test PRIVATE svn-lfs-export_lib Catch2::Catch2WithMain svn-lfs-export_warnings)
target_compile_features(svn-lfs-export_test PRIVATE cxx_std_23)

include(Catch)

catch_discover_tests(svn-lfs-export_test)
