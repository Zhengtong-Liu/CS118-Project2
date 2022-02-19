#define HEADER_SIZE 12
#define MAX_PAYLOAD_SIZE 512
#include <string.h>
using namespace std;
// Struct that stores header fields
struct Header {
	int sequenceNumber;
	int ackNumber;
	int connectionID;
	int ACK;
	int SYN;
	int FIN;
};

int safeportSTOI(string stringnumber)
{
	int result;
	try
	{
		result = stoi(stringnumber);
	}
	catch(const std::invalid_argument& ia)
	{
		cerr << "Error: Port number not valid: not convertable" << endl;
		exit(EXIT_FAILURE);
	}
	catch(const std::out_of_range& outrange)
	{
		cerr << "Error: Port number not valid: number of of int range" << endl;
		exit(EXIT_FAILURE);
	}
	if(result < 0 || result > 65535)
	{
		cerr << "Error: Port number not valid: shoud be between 0 - 65535" << endl;
		exit(EXIT_FAILURE);
	}
	return result;
}

// convert char array to int
// e.g.: 00000010 -> 4
int getIntFromCharArr (char * arr, int size) {
	int magnitude = 1;
	int num = 0;
	for (int i = 0; i < int(size); i++) {
		for (int j = 0; j < 8; j++) {
			if ((*(arr + i) & (1 << j)) != 0)
				num += magnitude;
			magnitude *= 2;
		}

	}
	
	return num;
}

// convert int to char array
// e.g.: 4 -> 00000010 
void setCharArrFromInt(int num, char * arr, int n_bytes) {
	for (int i = 0; i < n_bytes; i++) 
		*(arr + i) = 0;
	int magnitude = 0;
	while (num != 0) {
		if (num % 2 == 1) {
			*(arr + (magnitude / 8)) |= (1 << (magnitude % 8));
		}
		num /= 2;
		magnitude ++;
	}
}

// construct whole message from header and payload into buffer
void ConstructMessage(Header header, char * payload, char * buffer, int payloadSize) {
	setCharArrFromInt(header.sequenceNumber, buffer, 4);
	setCharArrFromInt(header.ackNumber, buffer + 4, 4);
	setCharArrFromInt(header.connectionID, buffer + 8, 2);
	char flagBit = 0;
	flagBit |= (header.ACK << 2 | header.SYN << 1 | header.FIN);
	buffer[10] = flagBit;
	if (payload)
		memcpy(buffer + HEADER_SIZE, payload, payloadSize);
}

// deconstruct whole message into header and payload
void DeconstructMessage(Header header, char * buffer) {
    header.sequenceNumber = getIntFromCharArr(buffer, 4);
	header.ackNumber = getIntFromCharArr(buffer + 4, 4);
	header.connectionID = getIntFromCharArr(buffer + 8, 2);
	header.ACK = (buffer[10] & 4) != 0;
	header.SYN = (buffer[10] & 2) != 0;
	header.FIN = (buffer[10] & 1) != 0;
}

// output debug message to std::out
void outputMessage(Header header, bool isClient) {
	cout << "RECV " << header.sequenceNumber << " " << header.ackNumber << " " << header.connectionID;
	if (isClient) 
		cout << " " << 0 << " " << 0;
	if (header.ACK)
		cout << " ACK";
	if (header.SYN)
		cout << " SYN";
	if (header.FIN)
		cout << " FIN";
	cout << endl;
}