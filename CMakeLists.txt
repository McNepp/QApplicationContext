cmake_minimum_required(VERSION 3.19)

project(QApplicationContext)
set(CMAKE_CXX_STANDARD 17)
enable_testing()

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif()


find_package(Qt6 REQUIRED COMPONENTS Core OPTIONAL_COMPONENTS Test)

FILE(GLOB sources src/*.cpp src/*.h defs/*.h)


add_library(${PROJECT_NAME} STATIC ${sources})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4 /w45262 /w44062)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/defs)

get_target_property(QApplicationContext_INCLUDE_DIRS ${PROJECT_NAME} INCLUDE_DIRECTORIES)
set(QApplicationContext_INCLUDE_DIRS ${QApplicationContext_INCLUDE_DIRS} PARENT_SCOPE)


target_link_libraries(${PROJECT_NAME} Qt6::Core)

FILE(GLOB test_sources ${CMAKE_CURRENT_LIST_DIR}/test/test*.cpp)

foreach(testfile ${test_sources})
    get_filename_component(testname ${testfile} NAME_WE)

    qt_add_executable(${testname} ${testfile} ${CMAKE_CURRENT_LIST_DIR}/test/appcontexttestclasses.cpp ${CMAKE_CURRENT_LIST_DIR}/test/appcontexttestclasses.h ${CMAKE_CURRENT_LIST_DIR}/test/registrationslot.h)
    if(MSVC)
      target_compile_options(${testname} PRIVATE /W4 /w45262 /w44062)
    endif()

    target_link_libraries(${testname} PRIVATE QApplicationContext Qt::Core Qt::Test)

    add_test(${testname} ${testname})
endforeach()







