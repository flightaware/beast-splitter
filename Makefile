CXX=g++
CXXFLAGS=-std=c++11 -Wall -Werror -O -g
LIBS=-lboost_system -lpthread

all: testharness

testharness: beast_message.o beast_input.o beast_output.o testharness.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f *.o testharness
