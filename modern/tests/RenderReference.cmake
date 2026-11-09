# Renderer reference test: the Act I town map drawn by WARP (Direct3D's software rasterizer, the same pixels on every
# run on one machine) must hash to the stored value. The image comes from the user's game data, so only its MD5 is kept.
#   cmake -DEXE=<D2Next.exe> -DGAME=<game folder> -DOUT=<work folder> -DEXPECTED=<file with the MD5> -P RenderReference.cmake
# After a deliberate renderer change, look at <OUT>/render-reference.bmp and put its MD5 (printed on failure) in EXPECTED.
if(NOT EXISTS "${GAME}/d2data.mpq")
  message("SKIP: no game folder at ${GAME}")
  return()
endif()
set(image "${OUT}/render-reference.bmp")
file(REMOVE "${image}")
execute_process(COMMAND "${EXE}" --game-dir "${GAME}" --map --smoke-test --no-audio --software --capture "${image}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0 OR NOT EXISTS "${image}")
  message(FATAL_ERROR "FAIL: the map was not drawn (exit ${result})\n${output}")
endif()
file(MD5 "${image}" actual)
file(STRINGS "${EXPECTED}" lines REGEX "^[0-9a-f]+$")
list(GET lines 0 expected)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "FAIL: the drawn map changed: MD5 ${actual}, expected ${expected} (image: ${image})")
endif()
message("PASS: the drawn map matches the reference (${actual})")
