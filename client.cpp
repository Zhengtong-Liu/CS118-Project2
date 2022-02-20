#include <string>
#include <cmath>
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
#include <time.h>

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
	int port = safeportSTOI(argv[2]);
	string file_path = argv[3];

	//==========================================ReadFile=============================
	// store the content of the file in a buffer
	// reference: https://www.cplusplus.com/doc/tutorial/files/
	string file_content = "";
	stringstream strBbuffer;
	ifstream f (file_path);
	if (f.is_open()) {
		strBbuffer << f.rdbuf();
		file_content = strBbuffer.str();
		// cout << "file_content: " << file_content;
		f.close();
	} else {
		cerr << "Unable to open file: " << file_path << endl;
		exit(EXIT_FAILURE);
	}
	//==========================================ReadFile-END=============================
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
	Header prevHeader = {0, 0, 0, 0, 0, 0};
	Header curHeader = {0, 0, 0, 0, 0, 0};

	// =========================== SYN =====================================
	curHeader.sequenceNumber = 12345;
	curHeader.ackNumber = 0;
	curHeader.connectionID = 0;
	curHeader.ACK = 0;
	curHeader.SYN = 1;
	curHeader.FIN = 0;


	// construct return message
	char out_msg [HEADER_SIZE];
	memset(out_msg, 0, sizeof(out_msg));
	ConstructMessage(curHeader, NULL, out_msg, 0);
	if ( (ret = send(sock, out_msg, sizeof(out_msg), 0)) < 0 ) {
		perror("send");
		exit(EXIT_FAILURE);
	}
	prevHeader = curHeader;

    
	// declare variables
	char payload [MAX_PAYLOAD_SIZE];
	char msgBuffer[BUFFER_SIZE];
	int payload_len = 0;
	int n_package_total = ceil(double(file_content.length()) / 512.0);
	int cur_package_number = 0;
	time_t two_seconds_counter = (time_t)(-1);
	bool sendResponse = true;

	while (1) {
		// receive messag and store in msgBuffer
		memset(msgBuffer, 0, BUFFER_SIZE);
		if ( (ret = recv(sock, msgBuffer, BUFFER_SIZE, 0)) < 0 ) {
			perror("recv");
			exit(EXIT_FAILURE);
		}

		// deconstruct message header to curHeader and print to std::out
		DeconstructMessage(curHeader, msgBuffer);
		outputMessage(curHeader, true);

		// All data sent, start FIN
		if (cur_package_number == n_package_total) {
			sendResponse = true;
			curHeader.SYN = 0;
			curHeader.ACK = 0;
			curHeader.FIN = 1;
			curHeader.sequenceNumber = curHeader.ackNumber;
			curHeader.ackNumber = 0;
			payload_len = 0;
		}

		// SYN ACK case, responde with ACK message and first data message
		else if (prevHeader.SYN && curHeader.SYN && curHeader.ACK) {
			sendResponse = true;
			payload_len = 0;
			curHeader.SYN = 0;
			curHeader.ACK = 1;
			curHeader.FIN = 0;
			curHeader.ackNumber = curHeader.sequenceNumber;
			curHeader.sequenceNumber = 12346;

			// responde with ack message
			memset(msgBuffer, 0, BUFFER_SIZE);
			ConstructMessage(curHeader, payload, msgBuffer, payload_len);
			if ( (ret = send(sock, msgBuffer, sizeof(out_msg), 0)) < 0 ) {
				perror("send");
				exit(EXIT_FAILURE);
			}
			// prepare for data transfer
			curHeader.ACK = 0;
			cur_package_number ++;
			payload_len = cur_package_number == n_package_total ? file_content.length() % 512 : 512;
		}

		// FIN ACK case, start waiting 2 seconds
		else if (prevHeader.FIN && curHeader.ACK) {
			sendResponse = false;
			payload_len = 0;
			if (two_seconds_counter == -1)
				two_seconds_counter = time(0);
		}

		// in 2 seconds waiting phase and receives FIN, responds with ACK
		else if (prevHeader.FIN && curHeader.FIN) {
			sendResponse = true;
			payload_len = 0;
			curHeader.ACK = 1;
			curHeader.SYN = 0;
			curHeader.FIN = 0;
			curHeader.ackNumber = curHeader.sequenceNumber + 1;
			curHeader.sequenceNumber = prevHeader.sequenceNumber + 1;
		}

		// The rest case, send subsequent package
		else {
			sendResponse = true;
			cur_package_number ++;
			payload_len = cur_package_number == n_package_total ? file_content.length() % 512 : 512;
		}

		if (sendResponse) {
			// construct the payload size of the current package
			memset(payload, 0, sizeof(payload));
			strncpy(payload, file_content.c_str() + 512 * (cur_package_number - 1), payload_len);
			payload[payload_len] = 0;
			// construct and send message to server
			memset(msgBuffer, 0, BUFFER_SIZE);
			ConstructMessage(curHeader, payload, msgBuffer, payload_len);
			if ( (ret = send(sock, msgBuffer, HEADER_SIZE + payload_len, 0)) < 0 ) {
				perror("send");
				exit(EXIT_FAILURE);
			}

			// Assign curHeader to prevHeader
			prevHeader = curHeader;
		}
	}

	return 0;
}
