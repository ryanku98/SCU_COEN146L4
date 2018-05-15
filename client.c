/*
	rdt3.0 UDP client

	Author: Ryan Ku
	Date: 22 February 2018
	Lab: Thursday 5:15pm - 8:00pm
	Description: 	This code acts as a client machine that sends data through
  					a socket using user datagram protocol (UDP) to a server machine 
					to copy over the contents of the source file under any given
					name. A random function is implemented to randomly set a false
					checksum value.

*/

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// GLOBAL VARIABLES
int socket_fd, count = 0;
struct timeval tv;
fd_set readfds;
socklen_t addr_size;
struct sockaddr_in serv_addr;
struct PACKET msg_packet, ack_packet, *msg_packet_pointer, *ack_packet_pointer;

int csum();
void createPacket(int length);
void createClosingPacket();
int sendMessage();
int receiveACK();
int seq_ack_num(int old);
bool goodACK();

struct HEADER
{
	int seq_ack;	// SEQ for data and ACK for acknowledgement
	int len;		// length of data in bytes (zero for ACK)
	int checksum;	// checksum calculated (by byte)
};

struct PACKET
{
	struct HEADER header;
	char data[10];
};

void setupSelect()
{
	fcntl(socket_fd, F_SETFL, O_NONBLOCK);
	FD_ZERO(&readfds);
	FD_SET(socket_fd, &readfds);
	tv.tv_sec = 2;
	tv.tv_usec = 0;
}

int csum()
{
	char checksum = 0;
	int i;
	char *packet = (char *)msg_packet_pointer;
	for(i = 0; i < sizeof(packet); i++)	checksum ^= packet[i];
	return (int) checksum;
}

void createPacket(int length)
{
	bool falseCSUM = rand() < RAND_MAX / 3; // 33% chance of setting a false checksum
	msg_packet.header.len = length + 1;
	msg_packet.header.checksum = 0;
	if(!falseCSUM)	msg_packet.header.checksum = csum(msg_packet);
	else			printf("\nSetting false rand() CSUM on message %i", count + 1);
}

void createClosingPacket()
{	// create message packet of length 0 to indicate end of file transfer
	msg_packet.header.seq_ack = seq_ack_num(msg_packet.header.seq_ack);
	msg_packet.header.len = 0;
	msg_packet.header.checksum = 0;
	memset(msg_packet.data, 0, sizeof(msg_packet.data));
	msg_packet.header.checksum = csum(msg_packet);
}

int sendMessage()
{
	count++;
	int bytes_sent = sendto(socket_fd, msg_packet_pointer, sizeof(msg_packet), 0, (struct sockaddr *)&serv_addr, addr_size);
	if(bytes_sent == -1)
	{
		perror("sendto() error");
		exit(EXIT_FAILURE);
	}
	printf("\nMessage #%i: %s\n", count, msg_packet.data);
	printf("Message #%i sent - length: %d - SEQ: %d - checksum: %d\n", count, msg_packet.header.len, msg_packet.header.seq_ack, msg_packet.header.checksum);
	return bytes_sent;
}

int receiveACK()
{
	int bytes_received = recvfrom(socket_fd, ack_packet_pointer, sizeof(ack_packet), 0, NULL, NULL);
	if(bytes_received == -1)
	{
		perror("recvfrom() error");
		exit(EXIT_FAILURE);
	}
	return bytes_received;
}

int seq_ack_num(int old)	{	return abs(old - 1);	}

bool goodACK()
{
	bool status = ack_packet.header.seq_ack == msg_packet.header.seq_ack;
	if(status)	printf("ACK received (%i), sending next messsage.\n", ack_packet.header.seq_ack);
	else		printf("NAK received (%i), resending message.\n", ack_packet.header.seq_ack);
	return status;
}

// main function takes in port #, IP address, source file name, and destination file name
int main(int argc, char* argv[])
{
	int ptons, bytes_sent, bytes_received, port_number, rv;
	addr_size = sizeof(serv_addr);
	msg_packet_pointer = &msg_packet;
	ack_packet_pointer = &ack_packet;

	// check number of inputs
	if(argc != 5)
	{
		printf("Error: Could not input files.\n");
		exit(EXIT_FAILURE);
	}

	// set up
	memset(msg_packet.data, '0', sizeof(msg_packet.data));
	memset(ack_packet.data, '0', sizeof(ack_packet.data));
	memset(&serv_addr, '0', sizeof(serv_addr));

	// create socket
	socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
	if(socket_fd == -1)
	{
		perror("socket() error");
		exit(EXIT_FAILURE);
	}

	// check port number
	if(argv[1] == NULL)
	{
		printf("Error: No Port Number.\n");
		exit(EXIT_FAILURE);
	}
	
	// set port number
	port_number = atoi(argv[1]);

	// configure address
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port_number);
	memset(serv_addr.sin_zero, 0, sizeof(serv_addr.sin_zero));

	ptons = inet_pton(AF_INET, argv[2], &serv_addr.sin_addr.s_addr);
	if(ptons == -1)
	{
		perror("inet_ptons() error");
		exit(EXIT_FAILURE);
	}

	// open source file and save destination file name
	FILE* srcfile = fopen(argv[3], "rb");
	if(srcfile == NULL)
	{
		perror("fopen() error");
		exit(EXIT_FAILURE);
	}

	// first send name of destination file
	strcpy(msg_packet.data, argv[4]);
	msg_packet.header.seq_ack = seq_ack_num(msg_packet.header.seq_ack);
	do
	{
		createPacket(strlen(argv[4]));
		do
		{	// timeout --> no data
			if(rv == 0)	printf("TIMEOUT: resending message\n");
			sendMessage();
			setupSelect();
			rv = select(socket_fd + 1, &readfds, NULL, NULL, &tv);
		} while(rv == 0);
		receiveACK();
	} while(!goodACK());

	// send contents of source file
	while(!feof(srcfile))
	{
		// write contents of source file onto payload buffer
		bytes_sent = fread(msg_packet.data, 1, sizeof(msg_packet.data), srcfile);
		msg_packet.header.seq_ack = seq_ack_num(msg_packet.header.seq_ack);
		do
		{
			createPacket(bytes_sent);
			do
			{	// timeout --> no data
				if(rv == 0)	printf("TIMEOUT: resending message\n");
				sendMessage();
				setupSelect();
				rv = select(socket_fd + 1, &readfds, NULL, NULL, &tv);
			} while(rv == 0);
			receiveACK();
		} while(!goodACK());

		memset(msg_packet.data, '\0', sizeof(msg_packet.data));
	}

	createClosingPacket();
	int i;
	for(i = 0; i < 3; i++) { sendMessage(); count--; }

	if(close(socket_fd) != 0)	perror("\nSocket close error");
	else						printf("\nSocket close: SUCCESS\n");

	printf("%s sent.\n", argv[3]);
	fclose(srcfile);
	exit(EXIT_SUCCESS);
}
