add_executable( bourt )
set_common_target_properties( bourt )

target_include_directories( bourt
    PRIVATE
        include
    PRIVATE
        source
)

target_sources( bourt
    PRIVATE
        source/main.cpp
        source/BourtBot.cpp
    PRIVATE
        include/BourtBot.h
)

target_link_libraries( bourt PRIVATE TelegramBotAPI )
target_link_libraries( bourt PRIVATE Qt6::Core Qt6::Network )
target_include_directories( bourt PRIVATE ${TelegramBotAPI_INCLUDE_DIR} )
