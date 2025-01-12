CXX = g++
CXXFLAGS = -std=c++14 -Wall -Wno-deprecated-declarations
LDFLAGS = -L/usr/local/opt/openldap/lib -I/usr/local/opt/openldap/include -lldap -llber -lpthread

SERVER = twmailerserver
CLIENT = twmailerclient

SERVER_SRC = twmailerserver.cpp
CLIENT_SRC = twmailerclient.cpp

SERVER_OBJ = $(SERVER_SRC:.cpp=.o)
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) -o $(SERVER) $(SERVER_OBJ) $(LDFLAGS)

$(CLIENT): $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) -o $(CLIENT) $(CLIENT_OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJ) $(CLIENT_OBJ)

.PHONY: all clean
