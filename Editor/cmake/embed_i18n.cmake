# cmake -DI18N_DIR=<dir> -DOUTPUT=<file.cpp> -P embed_i18n.cmake
# Embeds every <locale>/<file>.json under I18N_DIR and writes the table read by
# src/I18n.cpp: `g_i18nFiles[]` ({ "<locale>/<file>.json", data, size }) and `g_i18nFileCount`.
file(GLOB_RECURSE files RELATIVE "${I18N_DIR}" "${I18N_DIR}/*.json")
list(SORT files)
set(arrays "")
set(entries "")
set(index 0)
foreach(file ${files})
    file(READ "${I18N_DIR}/${file}" hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR size "${hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "((0x..,){32})" "\\1\n" bytes "${bytes}")
    string(APPEND arrays "static const unsigned char g_i18n_${index}[] = {\n${bytes}\n};\n")
    string(APPEND entries "    { \"${file}\", g_i18n_${index}, ${size} },\n")
    math(EXPR index "${index} + 1")
endforeach()
if(index EQUAL 0)
    message(FATAL_ERROR "No translations found in ${I18N_DIR}")
endif()
file(WRITE "${OUTPUT}"
    "// Generated from ${I18N_DIR} by embed_i18n.cmake. Do not edit.\n"
    "struct EmbeddedI18nFile { const char* path; const unsigned char* data; unsigned size; };\n"
    "extern const EmbeddedI18nFile g_i18nFiles[];\n"
    "extern const unsigned g_i18nFileCount;\n"
    "${arrays}"
    "const EmbeddedI18nFile g_i18nFiles[] = {\n${entries}};\n"
    "const unsigned g_i18nFileCount = ${index};\n")
