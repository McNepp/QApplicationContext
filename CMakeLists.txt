cmake_minimum_required(VERSION 3.19)

project(QApplicationContext)

enable_testing()

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(Qt6 REQUIRED COMPONENTS Core OPTIONAL_COMPONENTS Test)

FILE(GLOB sources src/*.cpp src/*.h defs/*.h)


add_library(${PROJECT_NAME} STATIC ${sources})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/defs)

get_target_property(QApplicationContext_INCLUDE_DIRS ${PROJECT_NAME} INCLUDE_DIRECTORIES)
set(QApplicationContext_INCLUDE_DIRS ${QApplicationContext_INCLUDE_DIRS} PARENT_SCOPE)


target_link_libraries(${PROJECT_NAME} Qt6::Core)



qt_add_executable(testqapplicationcontext ${CMAKE_CURRENT_LIST_DIR}/test/testqapplicationcontext.cpp ${CMAKE_CURRENT_LIST_DIR}/test/appcontexttestclasses.cpp)
target_link_libraries(testqapplicationcontext PRIVATE QApplicationContext Qt::Core Qt::Test)
add_Test(testqapplicationcontext testqapplicationcontext)
 




