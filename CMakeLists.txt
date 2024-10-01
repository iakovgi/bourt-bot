cmake_minimum_required( VERSION 3.29 )

project( Bourt LANGUAGES CXX )
set( CMAKE_AUTOMOC ON )
set( CMAKE_AUTOUIC ON )
set( CMAKE_AUTORCC ON )

add_compile_options( /Zc:__cplusplus )

find_package( Qt6 REQUIRED COMPONENTS Core Network )
#find_package( OpenSSL CONFIG REQUIRED )
#find_package( Crypto CONFIG REQUIRED )
add_subdirectory( third_party )

function( set_common_target_properties TARGET_NAME )
    set_property( TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 23 )
    set_property( TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD_REQUIRED ON )
    set_property( TARGET ${TARGET_NAME} PROPERTY CXX_EXTENSIONS OFF )
endfunction()

add_subdirectory( bourt )
