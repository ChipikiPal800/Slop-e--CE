# ----------------------------
# Makefile Options
# ----------------------------

NAME = SLOP
ICON = slopeicon.png
DESCRIPTION = "Slope for the TI-84+CE v.0.18 :3 by velkryn."
COMPRESSED = YES

EXTRA_C_SOURCES =
EXTRA_CXX_SOURCES =

# Stage 12 game and adapted original rolling-ball sprites.
SRCDIR = src

# Optimize for frame time; -Oz favors a smaller binary over the hot render loop. fuck you O2

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
