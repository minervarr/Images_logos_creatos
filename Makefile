CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -ffp-contract=off
LDLIBS ?= -lz

glyphpng: glyphpng.cpp
	$(CXX) $(CXXFLAGS) glyphpng.cpp -o $@ $(LDLIBS)

clean:
	rm -f glyphpng

.PHONY: clean
