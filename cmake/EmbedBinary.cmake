# mp_embed_binary(<file> <simbolo> <sorgente generato>): scrive un sorgente C
# con il contenuto di <file> come array di byte, <simbolo>, e la sua
# lunghezza, <simbolo>_size. Si rigenera quando <file> cambia.
function(mp_embed_binary input symbol output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    if(EXISTS "${output}" AND "${output}" IS_NEWER_THAN "${input}")
        return()
    endif()
    file(READ "${input}" hex HEX)
    file(SIZE "${input}" size)
    # Sedici byte per riga, ciascuno come 0xNN.
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "((0x..,){16})" "\\1\n" bytes "${bytes}")
    file(WRITE "${output}"
        "/* Generato da cmake/EmbedBinary.cmake: non modificare. */\n"
        "const unsigned char ${symbol}[${size}] = {\n${bytes}\n};\n"
        "const unsigned long ${symbol}_size = ${size};\n")
endfunction()

# mp_embed_binaries(<sorgente generato> <simbolo> <file>...): come sopra, per
# più file: un array per ciascuno e la tabella <simbolo> di mp_embedded_file
# (nome senza estensione, dati, lunghezza) nell'ordine dato, lunga
# <simbolo>_count.
function(mp_embed_binaries output symbol)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARGN})
    set(fresh TRUE)
    foreach(input ${ARGN})
        if(NOT EXISTS "${output}" OR NOT "${output}" IS_NEWER_THAN "${input}")
            set(fresh FALSE)
        endif()
    endforeach()
    if(fresh)
        return()
    endif()
    set(arrays "")
    set(table "")
    set(index 0)
    foreach(input ${ARGN})
        file(READ "${input}" hex HEX)
        file(SIZE "${input}" size)
        get_filename_component(name "${input}" NAME_WE)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
        string(REGEX REPLACE "((0x..,){16})" "\\1\n" bytes "${bytes}")
        string(APPEND arrays "static const unsigned char ${symbol}_${index}[${size}] = {\n${bytes}\n};\n")
        string(APPEND table "    {\"${name}\", ${symbol}_${index}, ${size}},\n")
        math(EXPR index "${index} + 1")
    endforeach()
    file(WRITE "${output}"
        "/* Generato da cmake/EmbedBinary.cmake: non modificare. */\n"
        "struct mp_embedded_file { const char* name; const unsigned char* data; unsigned long size; };\n"
        "${arrays}"
        "const struct mp_embedded_file ${symbol}[] = {\n${table}};\n"
        "const unsigned long ${symbol}_count = ${index};\n")
endfunction()
