# Convierte un fichero binario en una cabecera C++ con su contenido, para que
# el launcher lleve dentro sus imágenes y su fuente sin ficheros sueltos.
#
#   embed_file(<entrada> <cabecera de salida> <símbolo>)
#
# La cabecera define `<símbolo>[]` y `<símbolo>Size`. Se regenera al cambiar
# la entrada.
# Ruta de este script, guardada al incluirlo (CMAKE_CURRENT_FUNCTION_LIST_FILE
# es de CMake 3.17 y el proyecto admite 3.16).
set(_EMBED_FILE_SCRIPT "${CMAKE_CURRENT_LIST_FILE}")

function(embed_file input output symbol)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${input} -DOUTPUT=${output} -DSYMBOL=${symbol}
                -P "${_EMBED_FILE_SCRIPT}"
        DEPENDS "${input}" "${_EMBED_FILE_SCRIPT}"
        COMMENT "Embedding ${symbol}"
        VERBATIM)
endfunction()

if (CMAKE_SCRIPT_MODE_FILE AND DEFINED INPUT)
    file(READ "${INPUT}" hex HEX)
    string(LENGTH "${hex}" hexLength)
    math(EXPR size "${hexLength} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    # Una línea por cada 32 bytes, para que el fichero sea manejable.
    string(REGEX REPLACE "((0x..,){32})" "\\1\n" bytes "${bytes}")
    file(WRITE "${OUTPUT}"
        "// Generado por cmake/EmbedFile.cmake a partir de ${INPUT}. No editar.\n"
        "#pragma once\n#include <cstddef>\n"
        "alignas(16) static const unsigned char ${SYMBOL}[] = {\n${bytes}\n};\n"
        "static const std::size_t ${SYMBOL}Size = ${size};\n")
endif()
