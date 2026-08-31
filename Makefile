CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -ffp-contract=off
LDLIBS ?= -lz

glyphpng: glyphpng.cpp core/glyphpng_core.cpp core/glyphpng_core.hpp
	$(CXX) $(CXXFLAGS) glyphpng.cpp core/glyphpng_core.cpp -o $@ $(LDLIBS)

clean:
	rm -f glyphpng

.PHONY: clean
