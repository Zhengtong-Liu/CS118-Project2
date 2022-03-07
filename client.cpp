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
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ctime>
#include <netdb.h>


#include "helpers.h"

using namespace std;

int main(int argc, char* argv[])
{
	if (argc != 4) {
		cerr << "ERROR: Usage: " << argv[0] << " <HOSTNAME-OR-IP> <PORT> <FILENAME> " << endl;
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
		f.close();
	} else {
		cerr << "ERROR: Unable to open file: " << file_path << endl;
		exit(EXIT_FAILURE);
	}
	//==========================================ReadFile-END=============================
	//==========================================SocketCreation=============================
	int sock;

	// create socket fd
	if ( (sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0 ) {
		perror("ERROR: socket");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	bool hostname = false;
	// Convert IP address from string to binary form
	if( !inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr)){
		//referenced from https://man7.org/linux/man-pages/man3/getaddrinfo.3.html
		struct addrinfo hints;
		struct addrinfo *result, *rp;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = 0;
		hints.ai_protocol = 0;  
		hostname = true;
		int s;
		s = getaddrinfo(server_ip.c_str(), argv[2], &hints, &result);
		if (s != 0) {
			fprintf(stderr, "ERROR: getaddrinfo: %s\n", gai_strerror(s));
			exit(EXIT_FAILURE);
		}
		for (rp = result; rp != NULL; rp = rp->ai_next) {
			if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1)
			{
				break;             
			}
		}
		freeaddrinfo(result); 
		if (rp == NULL) {
			cerr << "ERROR: Wrong IP address" << endl;
			exit(EXIT_FAILURE);
		}
	}
	int ret;	
	if(!hostname)
	{
		if ( (ret = connect(sock, (sockaddr*)&server_addr, sizeof(server_addr))) < 0 ) {
			perror("ERROR: connect");
			exit(EXIT_FAILURE);
		}
	}
	//==========================================SocketCreation-END=============================

	// construct header
	Header prevHeader = {0, 0, 0, 0, 0, 0};
	Header curHeader = {0, 0, 0, 0, 0, 0};

	// declare variables
	char payload [MAX_PAYLOAD_SIZE]; // buffer to store payload
	char msgBuffer[BUFFER_SIZE]; // buffer to store received/sent message
	int expectedAckNumber = 0; // expected ack number from server
	int lastAckNumber = 0; // last Acked number from server
	int payloadSizeTotal = file_content.length(); // total size of payload
	int payloadSizeSent = 0; // size of payload sent
	int payloadSizeAcked = -1; // size of payload acked by server (initial -1 to avoid counting SYN)
	int payloadSizeCapacity = 0; // max capacity of cwnd for packet to be sent in this round
	int payloadSizeToBeSent = 0; // payload packet to be sent in this round

	clock_t two_seconds_timer = clock(); // FIN 2 second timer
	clock_t ten_seconds_timer = clock(); // 10 second timer
	clock_t retransmission_timer = clock(); // 0.5s retransmission timer
	clock_t start_time = clock(); // clock only works after they are activated after start_time

	bool sendMessage = true; // whether a message should be sent in this round
	CwndCnotroller * cwnd = new CwndCnotroller(12345); // cwnd controller
	ClientBufferController * bufferController = new ClientBufferController(); // client buffer controller

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
		perror("ERROR: send");
		exit(EXIT_FAILURE);
	}
	outputMessage(curHeader, "SEND", cwnd);
	prevHeader = curHeader;
	lastAckNumber = 12345;
	expectedAckNumber = 12346;

	// non-blocking
	int yes = 1;
	ioctl(sock, FIONBIO, (char*)&yes);
	// receiving loop
	while (1) {
		// receive messag and store in msgBuffer
		memset(msgBuffer, 0, BUFFER_SIZE);
		// non-blocking receive
		long ret = recv(sock, msgBuffer, BUFFER_SIZE, 0);
		if (ret == -1 && errno == EWOULDBLOCK) { // no message from server yet
			if (two_seconds_timer > start_time) {
				if ((clock() - two_seconds_timer) / (double) CLOCKS_PER_SEC >= 2) { // FIN times up, close connection
					if ( close(sock) < 0 ) {
						perror("ERROR: close");
						delete cwnd;
						delete bufferController;
						exit(EXIT_FAILURE);
					}
					break;
				}
			}
			else if (ten_seconds_timer > start_time && (clock() - ten_seconds_timer) / (double) CLOCKS_PER_SEC >= 10) { // 10s times up, close connection
				if ( close(sock) < 0 ) {
					perror("ERROR: close");
					delete cwnd;
					delete bufferController;
					exit(EXIT_FAILURE);
				}
				delete cwnd;
				delete bufferController;
				exit(ETIMEDOUT);
			}
			else if (retransmission_timer > start_time && (clock() - retransmission_timer) / (double) CLOCKS_PER_SEC >= 0.5) { // retransmission timeout
				cwnd ->	timeout();
				// retransmit last unacked packet
				char retransmitBuffer [MAX_PAYLOAD_SIZE] = {0};
				int retransMissionBufferSize = 0;
				bufferController -> getBuffer(lastAckNumber, retransmitBuffer, curHeader, retransMissionBufferSize);
				if ( (ret = send(sock, retransmitBuffer, retransMissionBufferSize, 0)) < 0 ) {
					perror("ERROR: send");
					delete cwnd;
					delete bufferController;
					exit(EXIT_FAILURE);
				}
				outputMessage(curHeader, "SEND", cwnd, true);
				retransmission_timer = clock();
			}
			continue;
		} 
		else if (ret < 0 ) { // error receiving message from server
			perror("ERROR: recv");
			delete cwnd;
			delete bufferController;
			exit(EXIT_FAILURE);
		}
		else { // message received, set up 10s timer
			ten_seconds_timer = clock();
		}
		// deconstruct message header to curHeader 
		DeconstructMessage(curHeader, msgBuffer);
		// print to std::out
		outputMessage(curHeader, "RECV", cwnd);
		// wrong ack number or duplicate ack
		if (expectedAckNumber > lastAckNumber && (curHeader.ackNumber > expectedAckNumber || (curHeader.ackNumber <= lastAckNumber && !curHeader.FIN))) {
			continue;
		}
		else if (expectedAckNumber < lastAckNumber && (curHeader.ackNumber > expectedAckNumber && (curHeader.ackNumber <= lastAckNumber && !curHeader.FIN))) {
			continue;
		}
		else { // update payloadSizeAcked and last acknowledged number
			payloadSizeAcked += curHeader.ackNumber - lastAckNumber;
			if (curHeader.ackNumber <= 512 && lastAckNumber > 512) // cycled around
				payloadSizeAcked += MAX_ACK;
			lastAckNumber = curHeader.ackNumber;
		}
		// packet is in order, update cwnd and cumack
		if (!(curHeader.SYN || curHeader.FIN)) {
			cwnd -> recvACK();
			cwnd -> update_cumack(curHeader.ackNumber);
		}
		// cout << payloadSizeAcked << " " << expectedAckNumber << " " << lastAckNumber << endl;

		retransmission_timer = clock();
		// All data sent, start FIN
		if (payloadSizeAcked == payloadSizeTotal) {
			payloadSizeAcked ++;
			sendMessage = true;
			expectedAckNumber ++; //to be changed
			// construct header
			curHeader.SYN = 0;
			curHeader.ACK = 0;
			curHeader.FIN = 1;
			curHeader.sequenceNumber = curHeader.ackNumber;
			curHeader.ackNumber = 0;
			// pseudo payload capacity to facilitate packet sending process
			payloadSizeCapacity = -1;
		}

		// SYN ACK case, responde with ACK message and first data message
		else if (prevHeader.SYN && curHeader.SYN && curHeader.ACK) {
			sendMessage = true;
			curHeader.SYN = 0;
			curHeader.ACK = 1;	// responde with ack message
			curHeader.FIN = 0;
			curHeader.ackNumber = curHeader.sequenceNumber + 1;
			curHeader.sequenceNumber = 12346;
			// get number of bytes to transfer in this round
			payloadSizeCapacity = cwnd->get_cwnd_size();
		}

		// FIN ACK case, start waiting 2 seconds
		else if (prevHeader.FIN) {
			if (curHeader.ACK) {
				sendMessage = false;
				if (two_seconds_timer <= start_time)
					two_seconds_timer = clock();
			}
			// in 2 seconds waiting phase and receives FIN, responds with ACK
			if (curHeader.FIN) {
				sendMessage = true;
				curHeader.ACK = 1;
				curHeader.SYN = 0;
				curHeader.FIN = 0;
				curHeader.ackNumber = curHeader.sequenceNumber + 1;
				curHeader.sequenceNumber = prevHeader.sequenceNumber + 1;
				// pseudo payload capacity to facilitate packet sending process
				payloadSizeCapacity = -1;
			}
		}

		else if (curHeader.FIN) {
			sendMessage = true;
			curHeader.ACK = 1;
			curHeader.SYN = 0;
			curHeader.FIN = 0;
			curHeader.ackNumber = curHeader.sequenceNumber + 1;
			curHeader.sequenceNumber = prevHeader.sequenceNumber + 1;
			// pseudo payload capacity to facilitate packet sending process
			payloadSizeCapacity = -1;
		}

		// The rest case, send subsequent packet
		else if (payloadSizeSent < payloadSizeTotal){
			sendMessage = true;
			curHeader.ACK = 0;
			curHeader.SYN = 0;
			curHeader.FIN = 0;
			curHeader.ackNumber = 0;
			curHeader.sequenceNumber = expectedAckNumber;	
			payloadSizeCapacity = max(0, cwnd->get_cwnd_size() - (payloadSizeSent - payloadSizeAcked));
		}
		if (sendMessage) {
			// construct the payload size of the current packet
			while (payloadSizeCapacity != 0) {
				// SYN/FIN case, no payload
				if (payloadSizeCapacity == -1) { 
					payloadSizeToBeSent = 0;
					payloadSizeCapacity = 0;
				}
				// wait for a whole 512 byte window to sent packet
				else if (payloadSizeCapacity < 512) break;
				// determine the current packet size
				else if (payloadSizeCapacity > MAX_PAYLOAD_SIZE) {
					if (payloadSizeTotal - payloadSizeSent > MAX_PAYLOAD_SIZE) {
						payloadSizeToBeSent = MAX_PAYLOAD_SIZE;
						payloadSizeCapacity -= MAX_PAYLOAD_SIZE;
					}
					else {
						payloadSizeToBeSent = payloadSizeTotal - payloadSizeSent;
						payloadSizeCapacity = 0;
					}
				}
				else {
					if (payloadSizeTotal - payloadSizeSent < payloadSizeCapacity) 
						payloadSizeToBeSent = payloadSizeTotal - payloadSizeSent;
					else
						payloadSizeToBeSent = payloadSizeCapacity;
					payloadSizeCapacity = 0;
				}
				// cout << payloadSizeCapacity << " " << payloadSizeAcked << " " << payloadSizeSent << " " << payloadSizeToBeSent << " " << payloadSizeTotal << endl;
				memset(payload, 0, MAX_PAYLOAD_SIZE);
				int length = file_content.copy(payload, payloadSizeToBeSent, payloadSizeSent);
  				payload[length] = '\0';
				payloadSizeSent += payloadSizeToBeSent;
				// construct and send message to server
				memset(msgBuffer, 0, BUFFER_SIZE);
				ConstructMessage(curHeader, payload, msgBuffer, payloadSizeToBeSent);
				if ( (ret = send(sock, msgBuffer, HEADER_SIZE + payloadSizeToBeSent, 0)) < 0 ) {
					perror("ERROR: send");
					delete cwnd;
					delete bufferController;
					exit(EXIT_FAILURE);
				}
				outputMessage(curHeader, "SEND", cwnd);
				// update expected ack number by payload size
				expectedAckNumber = (expectedAckNumber + payloadSizeToBeSent) % MAX_ACK;
				// store packet in buffer
				bufferController -> insertNewBuffer(curHeader.sequenceNumber, curHeader, msgBuffer, HEADER_SIZE + payloadSizeToBeSent);
				// Assign curHeader to prevHeader
				prevHeader = curHeader;
				// update sequence number
				curHeader.sequenceNumber = expectedAckNumber;
			}
		}
	}
	delete cwnd;
	delete bufferController;

	return 0;
}
