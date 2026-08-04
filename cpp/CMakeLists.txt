cmake_minimum_required(VERSION 4.4)
project("PocketGear")
add_library(PocketGear SHARED
            PocketGear.cpp
)
target_include_directories(PocketGear
        PUBLIC
        efmod-api/
        tefmod-api/
)
target_compile_options(PocketGear PRIVATE -std=c++17)
