CFLAGS += -g
all:
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) main.cpp util.cpp -o main
