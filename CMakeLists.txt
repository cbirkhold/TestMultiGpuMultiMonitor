
cmake_minimum_required(VERSION 3.5)
project(vmi-player)

add_executable(vmi-player main.cpp OpenGLUtilities.cpp)

target_include_directories(vmi-player PRIVATE $ENV{CUDA_PATH}/include)
target_include_directories(vmi-player PRIVATE sdks/glew/include)
target_include_directories(vmi-player PRIVATE sdks/glfw/include)
target_include_directories(vmi-player PRIVATE sdks/glm)
target_include_directories(vmi-player PRIVATE sdks/HWWrapper/include)
target_include_directories(vmi-player PRIVATE sdks/nvapi)
target_include_directories(vmi-player PRIVATE sdks/openvr/headers)

target_link_libraries(vmi-player OpenGL32 DXGI)
target_link_libraries(vmi-player $ENV{CUDA_PATH}/lib/x64/cuda.lib)
target_link_libraries(vmi-player ../sdks/glew/lib/Release/x64/glew32)
target_link_libraries(vmi-player ../sdks/glfw/lib-vc2017/glfw3dll)
target_link_libraries(vmi-player ../sdks/HWWrapper/lib/x64/HWWrapper)
target_link_libraries(vmi-player ../sdks/nvapi/amd64/nvapi64)
target_link_libraries(vmi-player ../sdks/openvr/lib/win64/openvr_api)
