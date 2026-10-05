# cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<name> -P embed_file.cmake
# Writes a C++ file defining `const unsigned char SYMBOL[]` and `const unsigned SYMBOL_size`.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR size "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# One line per 32 bytes keeps the generated file readable for the compiler.
string(REGEX REPLACE "((0x..,){32})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}"
    "// Generated from ${INPUT} by embed_file.cmake. Do not edit.\n"
    "extern const unsigned char ${SYMBOL}[];\n"
    "extern const unsigned ${SYMBOL}_size;\n"
    "alignas(4) const unsigned char ${SYMBOL}[] = {\n${bytes}\n};\n"
    "const unsigned ${SYMBOL}_size = ${size};\n")
