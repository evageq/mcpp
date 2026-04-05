CFLAGS += -g
SRC=$(wildcard ./*.cpp)
TARGET=main
all:
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(SRC) -o $(TARGET)
bear:
	rm $(TARGET); bear -- $(MAKE)
