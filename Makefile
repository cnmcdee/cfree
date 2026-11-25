CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
GTKFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTKLIBS  := $(shell pkg-config --libs gtk+-3.0)

fdesigner: src/main.cpp src/generator.cpp src/cmake_build.cpp src/project_io.cpp src/debugger.cpp src/debugger_ui.cpp src/preferences.cpp src/theme.cpp src/codeview.cpp src/model.h src/props.h src/events.h src/generator.h src/cmake_build.h src/project_io.h src/debugger.h src/debugger_ui.h src/preferences.h src/theme.h src/codeview.h src/indicators.h src/indicators_embedded.h src/drawings.h src/drawings_embedded.h ui/designer.ui
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp src/generator.cpp src/cmake_build.cpp src/project_io.cpp src/debugger.cpp src/debugger_ui.cpp src/preferences.cpp src/theme.cpp src/codeview.cpp $(GTKFLAGS) $(GTKLIBS)
src/indicators_embedded.h: src/indicators.h tools/embed_indicators.sh
	sh tools/embed_indicators.sh src/indicators.h $@

src/drawings_embedded.h: src/drawings.h tools/embed_drawings.sh
	sh tools/embed_drawings.sh src/drawings.h $@

clean:
	rm -f fdesigner

.PHONY: clean
