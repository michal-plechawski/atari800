cmake_minimum_required(VERSION 3.16)
project(artifacting_c_lib LANGUAGES C)

add_library(artifacting_c STATIC
	src/artifacting.c
	src/artifacting_filters.c
	src/artifacting_pal_scalar.c
	src/palettegenerator.c
	src/gtiatables.c
)

target_include_directories(artifacting_c PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(artifacting_c PUBLIC c_std_99)
