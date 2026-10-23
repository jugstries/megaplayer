# Esegue TOOL leggendo da INPUT e scrivendo su OUTPUT, in modo portabile
# (add_custom_command non garantisce le redirezioni della shell).
#   cmake -DTOOL=... -DINPUT=... -DOUTPUT=... -P RunWithRedirect.cmake
execute_process(
    COMMAND "${TOOL}"
    INPUT_FILE "${INPUT}"
    OUTPUT_FILE "${OUTPUT}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${TOOL} è fallito (${result})")
endif()
