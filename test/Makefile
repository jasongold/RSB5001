# Runs the game logic in rsb5001.ino on a normal computer.
#
#   cd test && make
#
# The Arduino IDE ignores this folder, so none of it ends up on the board.

CXXFLAGS = -std=c++17 -Wall -I. -I..

test: test.cpp Arduino.h LiquidCrystal.h ../rsb5001.ino
	$(CXX) $(CXXFLAGS) -x c++ test.cpp -o test
	./test

clean:
	rm -f test

.PHONY: clean
