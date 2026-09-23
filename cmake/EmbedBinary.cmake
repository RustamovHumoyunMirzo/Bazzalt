if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "EmbedBinary requires INPUT, OUTPUT, and SYMBOL")
endif()
file(READ "${INPUT}" _binary HEX)
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," _body "${_binary}")
get_filename_component(_output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")
file(WRITE "${OUTPUT}"
    "#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace Bazzalt::Runtime::Embedded {\ninline constexpr std::uint8_t ${SYMBOL}[] = {${_body}};\ninline constexpr std::size_t ${SYMBOL}Size = sizeof(${SYMBOL});\n}\n")
