# One interface target carries the warning flags, so every omaseed target agrees on them
# and third-party or Qt-generated targets are left alone.
add_library(omaseed-warnings INTERFACE)
add_library(omaseed::warnings ALIAS omaseed-warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(omaseed-warnings INTERFACE -Wall -Wextra)
    if(OMASEED_WERROR)
        target_compile_options(omaseed-warnings INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(omaseed-warnings INTERFACE /W4)
    if(OMASEED_WERROR)
        target_compile_options(omaseed-warnings INTERFACE /WX)
    endif()
endif()
