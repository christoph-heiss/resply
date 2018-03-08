cmake_minimum_required(VERSION 3.6)

project(resply VERSION 0.1.0 DESCRIPTION "Modern redis client for C++")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

option(BUILD_DOC "Build documentation using doxygen" ON)

add_definitions(-DASIO_STANDALONE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic-errors -Werror -Wall -Wextra")

# Apparently, protobuf uses __builtin_offsetof in a way which clang declares as
# a extension to the language - gcc not. Disable that warning to get it to compile.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-extended-offsetof")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

include_directories(include)

# https://github.com/chriskohlhoff/asio
include_directories($ENV{ASIO_INCLUDE_PATH})

# https://github.com/nlohmann/json
# should refer to the directory 'src' of json
include_directories($ENV{JSON_INCLUDE_PATH})

# https://github.com/muellan/clipp
include_directories($ENV{CLIPP_INCLUDE_PATH})

# https://github.com/gabime/spdlog
include_directories($ENV{SPDLOG_INCLUDE_PATH})


# protobuf
find_package(Protobuf REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)

include_directories(${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
file(GLOB protos protos/*.proto)

set(PROTO_PATH "${CMAKE_SOURCE_DIR}/protos")
set(GENERATED_PROTOBUF_PATH "${CMAKE_BINARY_DIR}")
set(GRPC_LIBRARIES grpc grpc++_unsecure)

foreach(proto ${protos})
        get_filename_component(proto_name ${proto} NAME_WE)
        add_custom_command(
                OUTPUT "${GENERATED_PROTOBUF_PATH}/${proto_name}.pb.h"
                       "${GENERATED_PROTOBUF_PATH}/${proto_name}.pb.cc"
                       "${GENERATED_PROTOBUF_PATH}/${proto_name}.grpc.pb.h"
                       "${GENERATED_PROTOBUF_PATH}/${proto_name}.grpc.pb.cc"
                COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                ARGS "--proto_path=${PROTO_PATH}"
                     "--cpp_out=${GENERATED_PROTOBUF_PATH}"
                     "${proto}"
                COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                ARGS "--proto_path=${PROTO_PATH}"
                     "--grpc_out=${GENERATED_PROTOBUF_PATH}"
                     "--plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}"
                     "${proto}"
        )

        list(APPEND GENERATED_PROTOBUF_FILES
                "${GENERATED_PROTOBUF_PATH}/${proto_name}.pb.h"
                "${GENERATED_PROTOBUF_PATH}/${proto_name}.pb.cc"
        )

        list(APPEND GENERATED_GRPC_FILES
                "${GENERATED_PROTOBUF_PATH}/${proto_name}.grpc.pb.h"
                "${GENERATED_PROTOBUF_PATH}/${proto_name}.grpc.pb.cc"
        )
endforeach(proto)

# Fix compilation of gRPC files
set_source_files_properties(${GENERATED_GRPC_FILES} PROPERTIES COMPILE_FLAGS -Wno-unused-parameter)


# libresply
add_library(libresply OBJECT src/libresply.cc src/resp-parser.cc)
target_compile_definitions(libresply PRIVATE RESPLY_VERSION="${PROJECT_VERSION}")

add_library(resply-shared SHARED $<TARGET_OBJECTS:libresply>)
add_library(resply-static STATIC $<TARGET_OBJECTS:libresply>)

set_target_properties(resply-static PROPERTIES OUTPUT_NAME resply)
set_target_properties(resply-shared PROPERTIES OUTPUT_NAME resply)

install(TARGETS resply-shared DESTINATION lib)
install(TARGETS resply-static DESTINATION lib)
install(FILES include/resply.h DESTINATION include)


# cli
add_executable(resply-cli src/resply-cli.cc)
target_link_libraries(resply-cli resply-static ${CMAKE_THREAD_LIBS_INIT})

add_executable(proto-cli src/proto-cli.cc ${GENERATED_PROTOBUF_FILES})
target_link_libraries(proto-cli ${PROTOBUF_LIBRARIES})

add_executable(grpc-cli src/grpc-cli.cc ${GENERATED_PROTOBUF_FILES} ${GENERATED_GRPC_FILES})
target_link_libraries(grpc-cli ${PROTOBUF_LIBRARIES} ${GRPC_LIBRARIES})

# proxy
add_executable(proxy src/proxy.cc ${GENERATED_PROTOBUF_FILES} ${GENERATED_GRPC_FILES})
target_link_libraries(proxy resply-static ${PROTOBUF_LIBRARIES} ${GRPC_LIBRARIES})


# tests
file(GLOB tests_source tests/*.cc)
foreach (test_source ${tests_source})
        get_filename_component(name ${test_source} NAME_WE)
        set(tests ${tests} ${name})

        add_executable(${name} ${test_source})
        target_link_libraries(${name} resply-static ${CMAKE_THREAD_LIBS_INIT})
        set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
endforeach ()

string(STRIP "${tests}" tests)

add_custom_target(
        tests
        COMMAND for t in ${tests}\; do printf %s \"$$t: \"\; ./tests/$$t > /dev/null\; [ $$? == 1 ] && echo success || echo failed\; done
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${tests}
        COMMENT "Running tests")

add_custom_target(
        tests-verbose
        COMMAND for t in ${tests}\; do echo -- Running $$t ...\; ./tests/$$t\; [ $$? == 1 ] && echo -- Test $$t succeeded || echo Test $$t failed\; done
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${tests}
        COMMENT "Running tests")


# documentation
find_package(Doxygen)
if (BUILD_DOC AND DOXYGEN_FOUND)
        set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

        add_custom_target(
                doc
                COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                COMMENT "Generating API documentation with Doxygen"
                VERBATIM)
else ()
        message("Doxygen need to be installed to generate the doxygen documentation")
endif ()
