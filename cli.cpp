#include <iostream>  
#include <stdlib.h>  
#include <netdb.h>  
#include <strings.h>  
#include <pthread.h>  
#include <unistd.h>  
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>  

using namespace std;

#define TRYTIME   100
#define ACCOUNTNUM  10
#define ACCOUNTSTART 101

// all message types  
enum msg_type
{
	ALIVE = 1,
	RESYNCH,
	RESYNCH_RET,	// resynch return result 
	RESYNCH_DONE,
	DEPOSIT,
	WITHDRAW,
	CHECK,
	HEARTBEAT,
	OP_DONE,
};

// print error message and exit 
void error_exit(const char* msg)
{
	cerr << msg << endl;
	exit(0);
}

// some member of the message structure will be unuseful 
typedef struct 
{
	int msg_type;
	int ival[3];
} MSG;

void make_msg(MSG* pMsg, msg_type type, int i1 = 0, int i2 = 0, int i3 = 0)
{
	pMsg->msg_type = (int)type;
	pMsg->ival[0] = i1;
	pMsg->ival[1] = i2;
	pMsg->ival[2] = i3;
}

int clisock = 0;

// UDP send and receive function 
void my_send(int sock, MSG& msg, struct sockaddr_in& addr)
{
	int ret = 0;
	ret = sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr*)&addr, sizeof(addr)); 
	if (ret < 0)
		error_exit("Sendto failed!");	
}

void my_recv(int sock, MSG& msg, struct sockaddr_in& addr)
{
	socklen_t len;
	int ret;
	len = sizeof(addr);	
	ret = recvfrom(sock, &msg, sizeof(msg), 0, (struct sockaddr*)&addr, &len); 
	if (ret < 0)	
		error_exit("Recvfrom failed!");
}

int main(int argc, char** argv)
{ 
	if (argc != 3)
		error_exit("Usage: ./client coordinator_ip coordinator_port");
	
	struct sockaddr_in address;
	bzero(&address, sizeof(address));  
	address.sin_family = AF_INET;  
	address.sin_addr.s_addr = inet_addr(argv[1]);
	address.sin_port = htons(atoi(argv[2]));  
	clisock = socket(AF_INET, SOCK_DGRAM, 0);
	if (clisock < 0)
		error_exit("Create client failed!");
	
	int i = 0;
	MSG msg;
	struct sockaddr_in tmp;
	while (i < TRYTIME)
	{
		int account = rand () % ACCOUNTNUM + ACCOUNTSTART;	
		// First always try to check 
		cout << "ID:" << account << " check" << endl;
		make_msg(&msg, CHECK, account);
		my_send(clisock, msg, address);
		// Try wait response 
		my_recv(clisock, msg, tmp);
		cout << "ID:" << msg.ival[0] << " balance " << msg.ival[1] << endl;

		// try to deposit 
		cout << "ID:" << account << " deposit 150" << endl;
		make_msg(&msg, DEPOSIT, account, 150);
		my_send(clisock, msg, address);
		// Try wait response 
		my_recv(clisock, msg, tmp);
		cout << "ID:" << msg.ival[0] << " deposit ok" << endl; 

		// try to withdraw  
		cout << "ID:" << account << " withdraw 100" << endl;
		make_msg(&msg, WITHDRAW, account, 100);
		my_send(clisock, msg, address);
		// Try wait response 
		my_recv(clisock, msg, tmp);
		cout << "ID:" << msg.ival[0] << " balance " << msg.ival[1] << endl; 

		i ++;

		usleep(1 * 1000000);
	}

	close(clisock);  
	return 0;  
}  

