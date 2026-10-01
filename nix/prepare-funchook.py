"""Use funchook's pinned, local capstone tree instead of its nested download."""
import pathlib
import sys

path = pathlib.Path(sys.argv[1]) / "CMakeLists.txt"
text = path.read_text()
start = text.index("  configure_file(cmake/capstone.cmake.in")
end = text.index("  string(TOUPPER ${FUNCHOOK_CPU}", start)
text = text[:start] + text[end:]
text = text.replace("${CMAKE_CURRENT_BINARY_DIR}/capstone-src", "${CMAKE_CURRENT_SOURCE_DIR}/capstone")
path.write_text(text)
