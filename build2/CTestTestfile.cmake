# CMake generated Testfile for 
# Source directory: C:/Users/dmclaurin/src/SmoothZoom
# Build directory: C:/Users/dmclaurin/src/SmoothZoom/build2
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[UnitTests]=] "C:/Users/dmclaurin/src/SmoothZoom/build2/Debug/smoothzoom_tests.exe")
  set_tests_properties([=[UnitTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/dmclaurin/src/SmoothZoom/CMakeLists.txt;251;add_test;C:/Users/dmclaurin/src/SmoothZoom/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[UnitTests]=] "C:/Users/dmclaurin/src/SmoothZoom/build2/Release/smoothzoom_tests.exe")
  set_tests_properties([=[UnitTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/dmclaurin/src/SmoothZoom/CMakeLists.txt;251;add_test;C:/Users/dmclaurin/src/SmoothZoom/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[UnitTests]=] "C:/Users/dmclaurin/src/SmoothZoom/build2/MinSizeRel/smoothzoom_tests.exe")
  set_tests_properties([=[UnitTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/dmclaurin/src/SmoothZoom/CMakeLists.txt;251;add_test;C:/Users/dmclaurin/src/SmoothZoom/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[UnitTests]=] "C:/Users/dmclaurin/src/SmoothZoom/build2/RelWithDebInfo/smoothzoom_tests.exe")
  set_tests_properties([=[UnitTests]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/dmclaurin/src/SmoothZoom/CMakeLists.txt;251;add_test;C:/Users/dmclaurin/src/SmoothZoom/CMakeLists.txt;0;")
else()
  add_test([=[UnitTests]=] NOT_AVAILABLE)
endif()
subdirs("_deps/catch2-build")
