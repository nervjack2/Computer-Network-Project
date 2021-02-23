CC = g++
OPENCV =  `pkg-config --cflags --libs opencv`
PTHREAD = -std=c++11 -pthread 

CLIENT = client.cpp
SERVER = server.cpp
CLI = client
SER = server

all: server client
  
server: $(SERVER)
	$(CC) $(SERVER) -o $(SER) $(PTHREAD)$  $(OPENCV)  
client: $(CLIENT)
	$(CC) $(CLIENT) -o $(CLI) $(PTHREAD)$  $(OPENCV) 
.PHONY: clean

clean:
	rm $(CLI) $(SER)
