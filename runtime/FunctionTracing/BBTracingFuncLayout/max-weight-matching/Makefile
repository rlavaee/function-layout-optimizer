CXX = clang++
all: matching

clean:
	rm -rf *.dSYM matching

matching: matching.cpp
	$(CXX) -Wall -g -ferror-limit=5 -std=c++11 -o $@ $<
