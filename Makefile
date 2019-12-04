CXX=g++
CXXFLAGS+=-std=c++11 -Wall -Werror -O -g
LIBS=-lboost_system -lboost_program_options -lboost_regex -lpthread

all: beast-splitter

beast-splitter: modes_message.o crc.o modes_filter.o beast_settings.o beast_input.o beast_input_serial.o beast_input_net.o beast_output.o status_writer.o splitter_main.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS)

format:
	clang-format -style=file -i *.cc *.h

clean:
	rm -f *.o beast-splitter
