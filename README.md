# CS118 Project 2

## Makefile

This provides a couple make targets for things.
By default (all target), it makes the `server` and `client` executables.

It provides a `clean` target, and `tarball` target to create the submission file as well.

You will need to modify the `Makefile` to add your userid for the `.tar.gz` turn-in at the top of the file.

## Provided Files

`server.cpp` and `client.cpp` are the entry points for the server and client part of the project.

## Academic Integrity Note

You are encouraged to host your code in private repositories on [GitHub](https://github.com/), [GitLab](https://gitlab.com), or other places.  At the same time, you are PROHIBITED to make your code for the class project public during the class or any time after the class.  If you do so, you will be violating academic honestly policy that you have signed, as well as the student code of conduct and be subject to serious sanctions.

## Wireshark dissector

For debugging purposes, you can use the wireshark dissector from `tcp.lua`. The dissector requires
at least version 1.12.6 of Wireshark with LUA support enabled.

To enable the dissector for Wireshark session, use `-X` command line option, specifying the full
path to the `tcp.lua` script:

    wireshark -X lua_script:./confundo.lua

To dissect tcpdump-recorded file, you can use `-r <pcapfile>` option. For example:

    wireshark -X lua_script:./confundo.lua -r confundo.pcap

## Contribution of each member

Zhengtong Liu: Implement basic socket communication, file I/O, server (how to handle incoming packets and send back messages, out-of-order packets, multiple clients)

Chenda Duan: Implement CWND window class and its functions, modified the file I/O functions, general input parsing (include the port number, hostname, etc). and general debug.

Wensi (Vince) Ai: Implement header construction / deconstruction, console logging outputs, most of client message processing logic (SYN/FIN, normal packet transmittion, how to detect loss packet and resent DUP, how to interact with cwnd class, detecting multiple ACKs, etc.), and debugged client issues.

## High Level Design

Helpers:

* Classes:

    ServerConnectionController -- store each client's information, including 
		```cpp
        		int ConnectionID;
		unordered_map <int, char*> payload_map;
		unordered_map <int, int> payload_length_map;
		clock_t shut_down_timer;
		clock_t retransmission_timer;
		int expectedSeqNum;
		int lastSentSeqNum;

		bool sentSYN;
		bool recvSYNACK;
		bool sentFIN;
		bool recvFINACK;

		Header SYN_header;
		Header FIN_header;

		sockaddr_in client_addr_info;

		CwndCnotroller* cwnd;
		```

    CwndCnotroller -- Control the Cwnd Size for the client
		```cpp
		int cwnd_size; // current cwnd window size
		int ssthresh; // current ssthresh size
		int max_cwnd; // max window allowed
		int cum_ack; // newest cum ack received from server
		int get_cwnd_size(); // return current cwnd size
		int get_ssthresh(); // return current ssthresh size
		void recvACK() //receive the ACK, update the cwnd size accordingly
		void timeout() //time out, reset cwnd size and the ssthresh size
		void update_cumack(); // record the updated cumulative Ack number, to help determine the range of the CWND window.
		```
* Functions:
    ```cpp
    int safeportSTOI(); // try to catch conversion error if a given string version of the port number cannot be converted to a valid port number
    int getIntFromCharArr(); // convert a decimal int variable into binary char array
    void setCharArrFromInt(); // convert a binary char array into decimal int variable
    void ConstructMessage(); // given the header class and the payload char array, convert the content into binary and put into the buffer to be sent
    void DeconstructMessage(); // Given the buffer in binary, get the current header info
    void outputMessage();  // given the header information, output to the stdout 
    ```

Server: 

* data structures: 	
    
        unordered_map<int, ServerConnectionController*> client_controller_map; // keep track of each client's info, indentified by connection ID
        unordered_map<int, bool> client_file_creation; // whether the file should be overwritten or appended
        vector<int> deleted_clientID; // deleted clients' connection IDs


* implementation:

    In the while loop, (1) check the timers (shut down timer and retransmission timer), (2) recv messages and construct header and payload,
    (3) drop incorrect packets, (4) construct response messages and write to file; (5) send the response msg


Client: 

* data structures: 	
    
       Client does not includes specially-declined data structures other than those from helpers.h. All of the newly declined data are in primitive format (int, boolean, char array, etc.)


* implementation:

    In the while loop, 
    
        (1) with non-blocking receive, check the timers (shut down timer, FIN timer, and retransmission timer), 
        
        (2) recv messages and deconstruct header, figure out whether it is normal, out-of-order, or DUP ACK
        
        (3) drop incorrect packets, send FIN/SIN if necessary, resent packet if necessary

        (4) update cum ack number, update cwnd and ssthread if necessary
        
        (5) while the cwnd is not smaller than 512 or we reach the last portion of the file:
            
            (5.1) construct response messages with payload
        
            (5.2) send the response msg, and store packet sent to buffer (for later retransmissions if necessary)

## Problems ran into

(1) we did not know how to handle multiple clients at the server end, and were confused about how to sent the timer; we initially thought we should use multi-threading, but a loop with a unordered map storing each client's information proved to work

(2) we kept meeting index out of range problem from the test script, after debugging we find that it's simply some unnecessary output from our debugging msg that draws the problem

(3) Initially we used time_t as the variable for timer, but we keep getting into wierd DUP situations, and the grading script would outputs cwnd too large error. After some debugging we figured out that time_t was in int format, which could leads to rounding problems. In the end we switched to clock_t and it finally worked.

## List of additional libraries

Server (only part of used libraries):

    #include <string>
    #include <iostream>
    #include <fstream>
    #include <unordered_map>
    #include <vector>
    #include <ctime>

Client:

    #include <string>
    #include <cmath>
    #include <thread>
    #include <fstream>
    #include <sstream>
    #include <string.h>
    #include <ctime>
    #include <netdb.h>

helpers.h:

    #include <string.h>
    #include <algorithm>
    #include <unordered_map>
    #include <time.h>
    #include <set>
    #include <unordered_map>
    #include <bitset>
    #include <utility>
## Acknowledgement

We would like to thank our TA, Xinyu Ma, for offering much help in debugging; most of the code references
are example code provided by TAs in discussion and official documentation online
