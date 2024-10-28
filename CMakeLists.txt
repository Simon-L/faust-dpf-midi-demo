# Based on https://github.com/DISTRHO/imgui-template-plugin
# See repo for authorship

cmake_minimum_required(VERSION 3.7)

set(CMAKE_CXX_STANDARD 17)

set(NAME faust-dpf-midi-demo)
project(${NAME})

add_subdirectory(dpf)

dpf_add_plugin(${NAME}
  TARGETS lv2 vst2 vst3
  FILES_DSP
    src/PluginDSP.cpp
)
  # FILES_UI
  #     src/PluginUI.cpp
  #     dpf-widgets/opengl/DearImGui.cpp)

target_include_directories(${NAME} PUBLIC src)
target_include_directories(${NAME} PUBLIC ${EXTRA_INCLUDES})
target_include_directories(${NAME} PUBLIC ${CMAKE_BINARY_DIR}/faust_gen)

target_compile_definitions(${NAME} PUBLIC POLYPHONY=8)

IF(NOT DEFINED FAUST_INVOCATION)
    set(FAUST_INVOCATION faust)
ENDIF()

IF(NOT DEFINED DSP_FILE)
    set(DSP_FILE ${CMAKE_SOURCE_DIR}/dsp.dsp)
ENDIF()

add_custom_command(
  OUTPUT faust_gen/dsp.hpp
  COMMENT "Generating dsp.hpp from the provided Faust file"
  COMMAND ${CMAKE_COMMAND} -E make_directory faust_gen
  COMMAND ${FAUST_INVOCATION} ${DSP_FILE} -o faust_gen/dsp.hpp
  DEPENDS ${DSP_FILE}
  VERBATIM)
add_custom_target(faust_gen DEPENDS faust_gen/dsp.hpp)

add_dependencies(faust-dpf-midi-demo faust_gen)

message("DSP_FILE is ${DSP_FILE}")
message("FAUST_INVOCATION is ${FAUST_INVOCATION}")