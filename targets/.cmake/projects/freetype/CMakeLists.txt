project(freetype2_osmand)

set(ROOT "${OSMAND_ROOT}/externals/freetype")
set(UPSTREAM "${ROOT}/upstream.patched")
include_directories(AFTER SYSTEM
	"${ROOT}"
	"${UPSTREAM}/include"
)
add_definitions(-DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=<ftmodule-override.h>)
if(CMAKE_COMPILER_FAMILY STREQUAL "gcc" OR CMAKE_COMPILER_FAMILY STREQUAL "clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
endif()
add_library(freetype2_osmand STATIC
	"${UPSTREAM}/src/base/ftbase.c"
	"${UPSTREAM}/src/base/ftbbox.c"
	"${UPSTREAM}/src/base/ftbitmap.c"
	"${UPSTREAM}/src/base/ftfstype.c"
	"${UPSTREAM}/src/base/ftgasp.c"
	"${UPSTREAM}/src/base/ftglyph.c"
	"${UPSTREAM}/src/base/ftinit.c"
	"${UPSTREAM}/src/base/ftlcdfil.c"
	"${UPSTREAM}/src/base/ftstroke.c"
	"${UPSTREAM}/src/base/ftsystem.c"
	"${UPSTREAM}/src/base/fttype1.c"
	"${UPSTREAM}/src/base/ftxf86.c"
	"${UPSTREAM}/src/autofit/autofit.c"
	"${UPSTREAM}/src/bdf/bdf.c"
	"${UPSTREAM}/src/cff/cff.c"
	"${UPSTREAM}/src/pshinter/pshinter.c"
	"${UPSTREAM}/src/psnames/psnames.c"
	"${UPSTREAM}/src/raster/raster.c"
	"${UPSTREAM}/src/sfnt/sfnt.c"
	"${UPSTREAM}/src/smooth/smooth.c"
	"${UPSTREAM}/src/truetype/truetype.c"
)
