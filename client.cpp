#include <string>
#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "helpers.h"

#define BUFFER_SIZE 1024

using namespace std;

int main(int argc, char* argv[])
{
	if (argc != 4) {
		cerr << "Usage: " << argv[0] << " <HOSTNAME-OR-IP> <PORT> <FILENAME> " << endl;
		return 1;
	}

	string server_ip = argv[1];
	int port = stoi(argv[2]);
	string file_path = argv[3];
	//==========================================FileIO=============================
	// store the content of the file in a buffer
	// reference: https://www.cplusplus.com/doc/tutorial/files/
	string file_content = "";
	stringstream buffer;
	ifstream f (file_path);
	if (f.is_open()) {
		buffer << f.rdbuf();
		file_content = buffer.str();
		// cout << "file_content: " << file_content;
		f.close();
	} else {
		cerr << "Unable to open file: " << file_path << endl;
		exit(EXIT_FAILURE);
	}
	//==========================================FileIO-END=============================
	//==========================================SocketCreation=============================
	int sock;

	// create socket fd
	if ( (sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0 ) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	
	// Convert IP address from string to binary form
	if( !inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr)){
		cerr << "Wrong IP address" << endl;
		exit(EXIT_FAILURE);
	}

	int ret;
	if ( (ret = connect(sock, (sockaddr*)&server_addr, sizeof(server_addr))) < 0 ) {
		perror("connect");
		exit(EXIT_FAILURE);
	}
	//==========================================SocketCreation-END=============================

	// construct header
	Header prevHeader {{0}};
	Header curHeader {{0}};

	// =========================== SYN =====================================
	setCharArrFromInt(12345, curHeader.sequenceNumber, 4);
	setCharArrFromInt(0, curHeader.ackNumber, 4);
	setCharArrFromInt(0, curHeader.connectionID, 2);
	int clientSequenceNumber = getIntFromCharArr(curHeader.sequenceNumber);
	int clientAckNumber = getIntFromCharArr(curHeader.ackNumber);
	int clientConnectionID = getIntFromCharArr(curHeader.connectionID);
	curHeader.ACK = 0;
	curHeader.SYN = 1;
	curHeader.FIN = 0;

	char payload [MAX_PAYLOAD_SIZE];
	memset(payload, 0, sizeof(payload));

	// construct return message
	char out_msg [HEADER_SIZE];
	memset(out_msg, 0, sizeof(out_msg));
	ConstructMessage(curHeader, NULL, out_msg, 0);
	if ( (ret = send(sock, out_msg, sizeof(out_msg), 0)) < 0 ) {
		perror("send");
		exit(EXIT_FAILURE);
	}
    
	char msgBuffer[BUFFER_SIZE];
	prevHeader = curHeader;

	while (1) {
		memset(msgBuffer, 0, BUFFER_SIZE);
		if ( (ret = recv(sock, msgBuffer, BUFFER_SIZE, 0)) < 0 ) {
			perror("recv");
			exit(EXIT_FAILURE);
		}
		DeconstructMessage(curHeader, payload, msgBuffer);
		if (prevHeader.SYN && curHeader.SYN && curHeader.ACK) {
			
		}
		
		cout << "sequenceNumber: " << clientSequenceNumber << endl;
		cout << "ackNumber: " << clientAckNumber << endl;
		cout << "connectionID: " << clientConnectionID << endl;
		cout << "ACK: " << curHeader.ACK << endl;
		cout << "SYN: " << curHeader.SYN << endl;
		cout << "FIN: " << curHeader.FIN << endl;
		cout << "Payload: " << msgBuffer + HEADER_SIZE << endl;

		int payload_len = (file_content.empty()) ? 0 : file_content.length();
		strcpy(payload, file_content.c_str());
		payload[payload_len] = 0;
	}

	if ( close(sock) < 0 ) {
		perror("close");
		exit(EXIT_FAILURE);
	}

	return 0;
}
