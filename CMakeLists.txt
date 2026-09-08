# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(embed_beetroot_hw_2_7_proj)

nvs_create_partition_image(nvs nvs.csv FLASH_IN_PROJECT)
