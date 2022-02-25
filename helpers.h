#include <string.h>
#include <algorithm>
#include <unordered_map>
#include <time.h>
#include <set>
#include <unordered_set>

#define HEADER_SIZE 12
#define MAX_PAYLOAD_SIZE 512
#define MAX_ACK 102400

using namespace std;
bool debug = false;
// Struct that stores header fields
struct Header {
	int sequenceNumber;
	int ackNumber;
	int connectionID;
	int ACK;
	int SYN;
	int FIN;
};
// class client_bufferSeq//store in / out of order Seq Number send from server to client
// {
// 	unordered_set<int> buffered_server_seq;
// 	int cum_seq = 0;
// 	public:
// 		client_bufferSeq(int initial_cum_seq):cum_seq(initial_cum_seq){};
// 		void inSertNewSeq(int newSeq)
// 		{
// 			if(newSeq == cum_seq+1)//if it is just increase by 1, simply add 1
// 				cum_seq += 1;
// 			else//else store it, and try to see if continuous cum_seq formed
// 			{
// 				buffered_server_seq.insert(newSeq);
// 				while(buffered_server_seq.find(cum_seq+1) != buffered_server_seq.end())
// 				{
// 					buffered_server_seq.erase(cum_seq+1);
// 					cum_seq++;
// 				}
// 			}
// 		}
// 		int getCumSeq()
// 		{
// 			return cum_seq;
// 		}
// };
// cwnd class
class Cwnd {
	int cwnd_size; // current cwnd window size
	int ssthresh; // current ssthresh size
	int max_cwnd; //max window allowed
	int cum_ack; //newest cum ack received from server
	public:
		Cwnd(int initial_ack):cwnd_size(512),ssthresh(10000),max_cwnd(51200),cum_ack(initial_ack){}
		int get_cwnd_size() //return current cwnd size 
		{
			return cwnd_size;
		}
		int get_ssthresh() //return current ssthresh
		{
			return ssthresh;
		}
		void recvACK() //recive the ACK
		{
			// slow start vs. congestion avoidance
			cwnd_size = cwnd_size < ssthresh ? min(cwnd_size+512, max_cwnd) : min(cwnd_size+((512*512)/cwnd_size), max_cwnd);
		}
		void timeout() //time out, reset cwnd size
		{
			ssthresh = cwnd_size / 2;
			cwnd_size = 512;
		}
		void update_cumack(int new_cumack)
		{
			cum_ack = new_cumack % MAX_ACK;
		}
		bool checkWithinCWND(int wantToSent)
		{
			if(wantToSent < MAX_ACK)
			{
				return (wantToSent > cum_ack) && (wantToSent < (cum_ack + cwnd_size));
			}
			else
			{
				return (wantToSent > cum_ack) &&  ((wantToSent % MAX_ACK) < ((cum_ack + cwnd_size) % MAX_ACK));
			}
		}
};


// manage connection with each client, identified by its connection ID
class ClientController {
	public:
		int ConnectionID;
		unordered_map <int, char*> payload_map;
		time_t timer;
		int expectedSeqNum;
		int lastSentSeqNum;

		ClientController(int cnID, int expectedSeq, int lastSentSeq) : ConnectionID(cnID), expectedSeqNum(expectedSeq), lastSentSeqNum(lastSentSeq) {
			timer = time(0);
		}

};

int safeportSTOI(string stringnumber) {
	int result;
	try
	{
		result = stoi(stringnumber);
	}
	catch(const invalid_argument& ia)
	{
		cerr << "ERROR: Port number not valid: not convertable" << endl;
		exit(EXIT_FAILURE);
	}
	catch(const out_of_range& outrange)
	{
		cerr << "ERROR: Port number not valid: number of of int range" << endl;
		exit(EXIT_FAILURE);
	}
	if(result < 0 || result > 65535)
	{
		cerr << "ERROR: Port number not valid: shoud be between 0 - 65535" << endl;
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
void DeconstructMessage(Header & header, char * buffer) {
    header.sequenceNumber = getIntFromCharArr(buffer, 4);
	header.ackNumber = getIntFromCharArr(buffer + 4, 4);
	header.connectionID = getIntFromCharArr(buffer + 8, 2);
	header.ACK = (buffer[10] & 4) != 0;
	header.SYN = (buffer[10] & 2) != 0;
	header.FIN = (buffer[10] & 1) != 0;
}

// output debug message to std::out
void outputMessage(Header header, string action, Cwnd * cwnd=NULL, bool isDuplicate=false) {
	cout << action << " " << header.sequenceNumber << " " << header.ackNumber << " " << header.connectionID;
	if (cwnd)  // client, needs to output cwnd and ssthresh
		cout << " " << cwnd->get_cwnd_size() << " " << cwnd->get_ssthresh();
	if (header.ACK)
		cout << " ACK";
	if (header.SYN)
		cout << " SYN";
	if (header.FIN)
		cout << " FIN";
	if (isDuplicate)
		cout << " DUP";
	cout << endl;
}