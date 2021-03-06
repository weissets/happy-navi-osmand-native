project(harfbuzz_osmand)

set(ROOT "${OSMAND_ROOT}/externals/harfbuzz")
set(UPSTREAM "${ROOT}/upstream.patched")
include_directories(AFTER SYSTEM
        "${UPSTREAM}/src"
)
if(CMAKE_COMPILER_FAMILY STREQUAL "gcc" OR CMAKE_COMPILER_FAMILY STREQUAL "clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif()
if(MSVC)
	set_source_files_properties("${UPSTREAM}/contrib/harfbuzz-unicode.c" PROPERTIES LANGUAGE CXX)
endif()
add_library(harfbuzz_osmand STATIC
	"${UPSTREAM}/src/harfbuzz-arabic.c"
	"${UPSTREAM}/src/harfbuzz-buffer.c"
	"${UPSTREAM}/src/harfbuzz-dump.c"
	"${UPSTREAM}/src/harfbuzz-gdef.c"
	"${UPSTREAM}/src/harfbuzz-gpos.c"
	"${UPSTREAM}/src/harfbuzz-greek.c"
	"${UPSTREAM}/src/harfbuzz-gsub.c"
	"${UPSTREAM}/src/harfbuzz-hangul.c"
	"${UPSTREAM}/src/harfbuzz-hebrew.c"
	"${UPSTREAM}/src/harfbuzz-impl.c"
	"${UPSTREAM}/src/harfbuzz-indic.cpp"
	"${UPSTREAM}/src/harfbuzz-khmer.c"
	"${UPSTREAM}/src/harfbuzz-myanmar.c"
	"${UPSTREAM}/src/harfbuzz-open.c"
	"${UPSTREAM}/src/harfbuzz-shaper.cpp"
	"${UPSTREAM}/src/harfbuzz-stream.c"
	"${UPSTREAM}/src/harfbuzz-thai.c"
	"${UPSTREAM}/src/harfbuzz-tibetan.c"
	"${UPSTREAM}/contrib/harfbuzz-unicode.c"
	"${UPSTREAM}/contrib/harfbuzz-unicode-tables.c"
)
