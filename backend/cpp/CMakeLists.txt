cmake_minimum_required(VERSION 3.18)
project(SmartRecyclingInspector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)

# Ubuntu 22.04's apt gRPC packages often provide pkg-config files but not
# gRPCConfig.cmake. Prefer the modern CMake package when it exists, and fall
# back to pkg-config inside Docker so `docker compose up --build` works on
# both x86_64 and Apple Silicon/aarch64 images.
find_package(gRPC CONFIG QUIET)
if(gRPC_FOUND)
  set(GRPCPP_LINK_TARGET gRPC::grpc++)
  set(GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
else()
  pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
  find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)
  set(GRPCPP_LINK_TARGET PkgConfig::GRPCPP)
endif()

find_package(OpenMP REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)

get_filename_component(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../proto/recycling.proto" ABSOLUTE)
get_filename_component(PROTO_PATH "${PROTO_FILE}" PATH)
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

set(PROTO_SRCS "${PROTO_GEN_DIR}/recycling.pb.cc")
set(PROTO_HDRS "${PROTO_GEN_DIR}/recycling.pb.h")
set(GRPC_SRCS "${PROTO_GEN_DIR}/recycling.grpc.pb.cc")
set(GRPC_HDRS "${PROTO_GEN_DIR}/recycling.grpc.pb.h")

add_custom_command(
  OUTPUT "${PROTO_SRCS}" "${PROTO_HDRS}" "${GRPC_SRCS}" "${GRPC_HDRS}"
  COMMAND protobuf::protoc
  ARGS --grpc_out "${PROTO_GEN_DIR}"
       --cpp_out "${PROTO_GEN_DIR}"
       -I "${PROTO_PATH}"
       --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
       "${PROTO_FILE}"
  DEPENDS "${PROTO_FILE}"
)

add_library(recycling_proto
  ${PROTO_SRCS}
  ${GRPC_SRCS}
)

target_include_directories(recycling_proto PUBLIC "${PROTO_GEN_DIR}")
target_link_libraries(recycling_proto PUBLIC protobuf::libprotobuf ${GRPCPP_LINK_TARGET})

add_library(inspector_engine
  src/inspector_engine.cpp
)

target_include_directories(inspector_engine PUBLIC include)
target_link_libraries(inspector_engine PUBLIC ${OpenCV_LIBS} OpenMP::OpenMP_CXX)

add_executable(recycling_server
  src/server.cpp
)

target_link_libraries(recycling_server PRIVATE recycling_proto inspector_engine ${GRPCPP_LINK_TARGET} ${OpenCV_LIBS} OpenMP::OpenMP_CXX)

add_executable(recycling_benchmark
  src/benchmark_client.cpp
)

target_link_libraries(recycling_benchmark PRIVATE recycling_proto ${OpenCV_LIBS} ${GRPCPP_LINK_TARGET} protobuf::libprotobuf)
