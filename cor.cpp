#include <fstream>  
#include <iostream>  
#include <stdlib.h>  
#include <stdio.h>  
#include <netdb.h>  
#include <strings.h>  
#include <unistd.h>  
#include <assert.h>  
#include <sys/time.h>  
#include <vector>
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>  

using namespace std;

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

// server information 
typedef struct 
{
	string ip;
	int port;
} svr_cfg;

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

typedef struct 
{
	MSG msg;	// must record the return address 
	struct sockaddr_in addr;
} cli_msg;

typedef struct 
{
	MSG resp;	// response message 
	int respnum;	// response already received 
	int account;
	bool inquery;
	vector<cli_msg> msgs;
} msg_queue;

// 3 servers 
struct sockaddr_in allserv[4];
MSG msg;
int servsock;  
msg_queue queues[ACCOUNTNUM];

// reachable server number: 2 or 3 
int svr_num = 0;
// in resync state 
bool resync = false;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 

// print error message and exit 
void error_exit(const char* msg)
{
	cerr << msg << endl;
	exit(0);
}

// there should be 3 servers 
void get_cfg(const char* fn, svr_cfg cfsg[4])
{
	ifstream ifs(fn);
	if (!ifs)
		error_exit("open server config failed");
	for (int i = 0; i < 4; ++ i)
		ifs >> cfsg[i].ip >> cfsg[i].port;
}

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

// serach the message queue for given account 
int search(int acc)
{
	for (int i = 0; i < ACCOUNTNUM; ++ i)
	{
		if (queues[i].account == acc)
			return i;
	}
	return -1;
}

// create a new thread to check the servers 
void* do_heartbeat(void* arg)
{
	int ret;
	struct sockaddr_in addr;
	int sock = socket(AF_INET, SOCK_DGRAM, 0);	// use a new socket to avoid contest    
	if (sock < 0)
		error_exit("Create socket failed!");
	
	MSG msg;	// overide the global variable msg 
	fd_set rdfds;
	struct timeval tm;
	while (1)
	{
		if (resync)
		{
			usleep(50000);
			continue;
		}
		int count = 0;
		make_msg(&msg, HEARTBEAT);
		for (int i = 0; i < 3; ++ i)
			my_send(sock, msg, allserv[i]);
		while (1)
		{
			FD_ZERO(&rdfds);	
			FD_SET(sock, &rdfds);
			tm.tv_sec = 0;
			tm.tv_usec = 350000;	// 350ms
			ret = select(sock + 1, &rdfds, NULL, NULL, &tm);
			if (ret < 0)
				error_exit("Select wrong!");
			if (ret == 0)
				break;
			my_recv(sock, msg, addr);
			count ++;
		}
		pthread_mutex_lock(&mutex);
		svr_num = count;
		pthread_mutex_unlock(&mutex);
	}
	return NULL;
}

// refresh all account query state 
void refresh()
{
	int count = 0;
	pthread_mutex_lock(&mutex);
	count = svr_num;
	pthread_mutex_unlock(&mutex);

	for (int i = 0; i < ACCOUNTNUM; ++ i)
	{
		msg_queue& que = queues[i];
		if (! que.inquery)
			continue;
		if (que.respnum >= count)
		{
			// send client response 
			cli_msg& climsg = que.msgs[0];
			// build the message to send back 
			msg = que.resp;
			my_send(servsock, msg, climsg.addr);
			// remove first message 
			vector<cli_msg>::iterator it = que.msgs.begin();
			que.msgs.erase(it);
			
			que.inquery = false;
		}
	}
}

// try request to server
void do_request()
{
	if (resync)	// do not send any request any more, lock all requests  
		return;
	for (int i = 0; i < ACCOUNTNUM; ++ i)
	{
		if (queues[i].inquery)
			continue;
		if (queues[i].msgs.size() == 0)
			continue;
		// try send the message to all the servers 
		msg = queues[i].msgs[0].msg;
		for (int j = 0; j < 3; ++ j)
			my_send(servsock, msg, allserv[j]);
		queues[i].inquery = true;
		queues[i].respnum = 0;
	}
}

// add a new request to the queue 
void add_msg(struct sockaddr_in addr)
{
	static int requestid = 0;	
	int account = msg.ival[0];
	int index = search(account);
	assert(index >= 0);
	
	cli_msg tmpmsg;
	tmpmsg.msg = msg;
	// each client request does not use this field 
	tmpmsg.msg.ival[2] = ++requestid;	
	tmpmsg.addr = addr;
	queues[index].msgs.push_back(tmpmsg);	// just add a new message 
}

void loop()
{
	int ret;
	struct sockaddr_in addr;
	fd_set rdfds;
	struct timeval tm;
	struct sockaddr_in resyncsvr;
	while (1)
	{
		FD_ZERO(&rdfds);	
		FD_SET(servsock, &rdfds);
		tm.tv_sec = 0;
		tm.tv_usec = 100000;	// 100ms
		ret = select(servsock + 1, &rdfds, NULL, NULL, &tm);
		if (ret > 0)
		{
			my_recv(servsock, msg, addr);
			switch (msg.msg_type)
			{
				case ALIVE:
					{
						resync = true;
						resyncsvr = addr;
						break;
					}
				case RESYNCH_DONE:
					{
						resync = false;
						break;
					}
				case DEPOSIT: 
				case WITHDRAW: 
				case CHECK: 
					add_msg(addr);
					break;
				case OP_DONE:
					{
						int acc = msg.ival[0];
						int index = search(acc);
						msg_queue& que = queues[index];
						if (que.msgs.size() > 0)	// or else ignore this message 
						{
							if (msg.ival[2] == que.msgs[0].msg.ival[2])	// or else ignore this message 
							{
								que.resp = msg;
								que.respnum ++;
							}
						}
						break;
					}
				default:
					error_exit("Receive unknown request!");
			}
		}
		
		// try update queue status 
		refresh();
		do_request();
	}
}

int main(int argc, char** argv)
{  
	if (argc != 2)
		error_exit("Usage: ./coordinator [ServerCfg]");
	// read the configs 
	svr_cfg cfgs[4];
	get_cfg(argv[1], cfgs);

	for (int i = 0; i < 4; ++ i)
	{
		bzero(&allserv[i], sizeof(allserv[i]));
		allserv[i].sin_family = AF_INET;
		if (i == 3)
			allserv[i].sin_addr.s_addr = htonl(INADDR_ANY); 
		else
			allserv[i].sin_addr.s_addr = inet_addr(cfgs[i].ip.c_str());
		allserv[i].sin_port = htons(cfgs[i].port);  
	}

	// start server 
	servsock = socket(AF_INET, SOCK_DGRAM, 0);  
	if (servsock < 0)
		error_exit("Create socket failed!");
	int on = 1;	// set reuseaddr 
	setsockopt(servsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (bind(servsock, (struct sockaddr *)&allserv[3], sizeof(allserv[3])) < 0)
		error_exit("Bind failed!"); 
	
	// initial all the queues 
	for (int i = 0; i < ACCOUNTNUM; ++ i)
	{
		queues[i].account = ACCOUNTSTART + i;
		queues[i].inquery = false;
		queues[i].respnum = 0;
	}
	
	// create a new thread to check server count 
	pthread_t tid;
	if (pthread_create(&tid, NULL, do_heartbeat, NULL) < 0)
		error_exit("Create thread failed!");

	// handle the request by coordinator and other 2 servers 
	loop();

	close(servsock);  

	return 0;  
} 

