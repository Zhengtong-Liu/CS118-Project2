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

	// declare variables
	char payload [MAX_PAYLOAD_SIZE]; // buffer to store payload
	char msgBuffer[BUFFER_SIZE]; // buffer to store received/sent message
	int expectedAckNumber = 0; // expected ack number from server
	int lastAckNumber = 0; // last Acked number from server

	int payloadSizeTotal = file_content.length(); // total size of payload
	int payloadSizeSent = 0; // size of payload sent
	int payloadSizeAcked = 0; // size of payload acked by server
	int payloadSizeCapacity = 0; // max capacity of cwnd for packet to be sent in this round
	int payloadSizeToBeSent = 0; // payload packet to be sent in this round

	time_t two_seconds_timer = (time_t)(-1); // FIN 2 second timer
	time_t ten_seconds_timer = (time_t)(-1); // 10 second timer
	time_t retransmission_timer = (time_t)(-1); // 0.5s retransmission timer

	bool sendMessage = true; // whether a message should be sent in this round
	bool hasPayload = true; // whether the packet sent this round has payload
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
		perror("send");
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
			messageReceived = false;
			if (two_seconds_timer != -1 && time(0) - two_seconds_timer >= 2) { // FIN times up, close connection
				if ( close(sock) < 0 ) {
					perror("close");
					exit(EXIT_FAILURE);
				}
				break;
			}
			else if (ten_seconds_timer != -1 && time(0) - ten_seconds_timer >= 10) { // 10s times up, close connection
				if ( close(sock) < 0 ) {
					perror("close");
					exit(EXIT_FAILURE);
				}
				exit(ETIMEDOUT);
			}
			else if (retransmission_timer != -1 && time(0) - retransmission_timer >= 0.5) { // retransmission timeout
				cwnd ->	timeout();
				// retransmit last unacked packet
				char retransmitBuffer [MAX_PAYLOAD_SIZE];
				bufferController -> getBuffer(lastAckNumber, retransmission_timer, curHeader);
				if ( (ret = send(sock, retransmitBuffer, sizeof(retransmitBuffer), 0)) < 0 ) {
					perror("send");
					exit(EXIT_FAILURE);
				}
				outputMessage(curHeader, "SEND", cwnd, true);
			}
			continue;
		} 
		else if (ret < 0 ) { // error receiving message from server
			perror("recv");
			exit(EXIT_FAILURE);
		}
		else { // message received, set up 10s timer
			messageReceived = true;
			ten_seconds_timer = time(0);
		}
		// deconstruct message header to curHeader and print to std::out
		DeconstructMessage(curHeader, msgBuffer);
		outputMessage(curHeader, "RECV", cwnd);
		// wrong ack number || duplicate ack
		if (curHeader.ackNumber  > expectedAckNumber || (curHeader.ackNumber == lastAckNumber && !curHeader.FIN))
			continue;
		// update last acknowledged number
		lastAckNumber = lastAckNumber < curHeader.ackNumber ? curHeader.ackNumber : lastAckNumber;
		// packet is in order, update cwnd and cumack
		cwnd -> recvACK();
		cwnd -> update_cumack(curHeader.ackNumber);
		retransmission_timer = time(0);
		// All data sent, start FIN
		if (payloadSizeAcked == payloadSizeTotal) {
			payloadSizeAcked ++;
			sendMessage = true;
			hasPayload = false;
			payload_len = 0;
			expectedAckNumber ++;//to be changed
			// construct header
			curHeader.SYN = 0;
			curHeader.ACK = 0;
			curHeader.FIN = 1;
			curHeader.sequenceNumber = curHeader.ackNumber;
			curHeader.ackNumber = 0;
		}

		// SYN ACK case, responde with ACK message and first data message
		else if (prevHeader.SYN && curHeader.SYN && curHeader.ACK) {
			sendMessage = true;
			hasPayload = true;
			payload_len = 0;
			curHeader.SYN = 0;
			curHeader.ACK = 1;	// responde with ack message
			curHeader.FIN = 0;
			curHeader.ackNumber = curHeader.sequenceNumber + 1;
			curHeader.sequenceNumber = 12346;
			// get number of bytes to transfer in this round
			payloadSizeCapacity = cwnd->get_cwnd_size();
		}

		// FIN ACK case, start waiting 2 seconds
		else if (prevHeader.FIN && curHeader.ACK) {
			sendMessage = false;
			payload_len = 0;
			if (two_seconds_timer == -1)
				two_seconds_timer = time(0);
		}

		// in 2 seconds waiting phase and receives FIN, responds with ACK
		else if (prevHeader.FIN && curHeader.FIN) {
			sendMessage = true;
			hasPayload = false;
			payload_len = 0;
			curHeader.ACK = 1;
			curHeader.SYN = 0;
			curHeader.FIN = 0;
			curHeader.ackNumber = curHeader.sequenceNumber + 1;
			curHeader.sequenceNumber = prevHeader.sequenceNumber + 1;
		}

		// The rest case, send subsequent packet
		else if (payloadSizeSent < payloadSizeTotal){
			sendMessage = true;
			hasPayload = true;
			curHeader.ACK = 0;
			curHeader.SYN = 0;
			curHeader.FIN = 0;
			cur_ack = curHeader.ackNumber;
			curHeader.ackNumber = curHeader.sequenceNumber;
			curHeader.sequenceNumber = cur_ack;	
			payloadSizeCapacity = cwnd->get_cwnd_size() - (payloadSizeSent - payloadSizeAcked);
		}
	}
	if (sendMessage) {
		// construct the payload size of the current packet
		while (!hasPayload || payloadSizeCapacity > 0) {
			if (!hasPayload) // SYN/FIN case, no payload
				payloadSizeToBeSent = 0;
			// determine the current packet size
			else if (payloadSizeCapacity > MAX_PAYLOAD_SIZE) {
				if (payloadSizeTotal - payloadSizeSent > MAX_PAYLOAD_SIZE)
					payloadSizeToBeSent = MAX_PAYLOAD_SIZE;
				else
					payloadSizeToBeSent = payloadSizeTotal;
			}
			else {
				if (payloadSizeTotal - payloadSizeSent < payloadSizeCapacity)
					payloadSizeToBeSent = payloadSizeTotal - payloadSizeSent;
				else
					payloadSizeToBeSent = payloadSizeCapacity;
			}
			memset(payload, 0, sizeof(payload));
			strncpy(payload, file_content.c_str() + payloadSizeSent, payloadSizeToBeSent);
			payloadSizeSent += payloadSizeToBeSent;
			payloadSizeCapacity -= payloadSizeToBeSent;
			// construct and send message to server
			memset(msgBuffer, 0, BUFFER_SIZE);
			ConstructMessage(curHeader, payload, msgBuffer, payloadSizeToBeSent);
			if ( (ret = send(sock, msgBuffer, HEADER_SIZE + payloadSizeToBeSent, 0)) < 0 ) {
				perror("send");
				exit(EXIT_FAILURE);
			}
			outputMessage(curHeader, "SEND", cwnd);
			// update expected ack number by payload size
			expectedAckNumber = (expectedAckNumber + payloadSizeToBeSent) % MAX_ACK;
			// store packet in buffer
			bufferController -> insertNewBuffer(curHeader.seqNumber, payload, curHeader);
			// Assign curHeader to prevHeader
			prevHeader = curHeader;
			// update sequence number
			curHeader.sequenceNumber = prevHeader.sequenceNumber + payloadSizeToBeSent;
		}
	}

	return 0;
}
