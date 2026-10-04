# Runs lines.py export-subtitles on the data edited by editor_tests and compares
# it with the editor's own export.
execute_process(
    COMMAND ${PYTHON} ${REPO}/tools/lines/lines.py --data ${OUT}/rt/lines export-subtitles -o ${OUT}/python_subtitles.json
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "lines.py export-subtitles failed")
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files ${OUT}/python_subtitles.json ${OUT}/editor_subtitles.json
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "editor export differs from lines.py export-subtitles")
endif()
