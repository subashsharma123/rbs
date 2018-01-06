#include <iostream>  
#include <fstream>  
#include <stdlib.h>  
#include <netdb.h>  
#include <strings.h>  
#include <string>  
#include <vector>  
#include <unistd.h>  
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>  

using namespace std;

#define LOGFILE     "oper.log" 
#define ACCOUNTFILE "account" 
#define ACCOUNTNUM  10

// all message types  
enum message_type
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
} server_config;

void error_exit(const char* msg)
{
	cerr << msg << endl;
	exit(0);
}

// there should be 3 servers and coordinator configuration  
void get_cfg(const char* fn, server_config cfsg[4])
{
	ifstream ifs(fn);
	if (!ifs)
		error_exit("open server config failed");
	for (int i = 0; i < 4; ++ i)
		ifs >> cfsg[i].ip >> cfsg[i].port;
}

typedef struct 
{
	int serial;
	int account;
	int balance;
} operation;

void get_log(vector<operation>& recs)
{
	recs.clear();
	ifstream ifs(LOGFILE);
	if (!ifs) 
		return;
	string str;
	ifs >> str >> str >> str >> str;	// headers 
	int opid, accid, bal;
	while (ifs >> opid >> accid >> bal)
	{
		operation rec;
		rec.serial = opid;
		rec.account = accid;
		rec.balance = bal;
		recs.push_back(rec);
	}
}

typedef struct 
{
	int msg_type;
	int ival[3];
} MSG;

void make_msg(MSG* pMsg, message_type type, int i1 = 0, int i2 = 0, int i3 = 0)
{
	pMsg->msg_type = (int)type;
	pMsg->ival[0] = i1;
	pMsg->ival[1] = i2;
	pMsg->ival[2] = i3;
}

// Read all clients information 
typedef struct 
{
	int account;
	int balance;
} acc_info;

void account_read(acc_info info[])
{
	ifstream ifs(ACCOUNTFILE);
	int num = 0;
	while (ifs >> info[num].account >> info[num].balance)
		num ++;	
}

void account_write(acc_info info[])
{
	ofstream ofs(ACCOUNTFILE);
	for (int i = 0; i < ACCOUNTNUM; ++ i)
		ofs << info[i].account << "\t" << info[i].balance << endl;
}

// 3 servers and 1 coordinator 
struct sockaddr_in allserv[4];
MSG msg;
int lastop = 0;	
// server socket 
int servsock;  
acc_info infos[ACCOUNTNUM];

// UDP send and receive function 
void my_send(int sock, struct sockaddr_in& addr)
{
	int ret = 0;
	ret = sendto(sock, &msg, sizeof(msg), 0, (struct sockaddr*)&addr, sizeof(addr)); 
	if (ret < 0)
		error_exit("Sendto failed!");	
}

void my_recv(int sock, struct sockaddr_in& addr)
{
	socklen_t len;
	int ret;
	len = sizeof(addr);	
	ret = recvfrom(sock, &msg, sizeof(msg), 0, (struct sockaddr*)&addr, &len); 
	if (ret < 0)	
		error_exit("Recvfrom failed!");
}

// search account 
int search(int acc)
{
	for (int i = 0; i < ACCOUNTNUM; ++ i)
	{
		if (infos[i].account == acc)
			return i;
	}
	return -1;
}

// add a new record to the log file 
void write_log(int acc, int balance)
{
	FILE* fp = fopen(LOGFILE, "a");
	if (!fp)
		error_exit("Add log failed!");
	if (lastop == 0)
	{
		fprintf(fp, "Operation#\tAccount#\tBalance\n");
		fprintf(fp, "----------------------------------\n");
	}
	char line[256];
	fprintf(fp, "%d\t\t%d\t\t%d\n", ++lastop, acc, balance);
	fclose(fp);
}

void remove_log()
{
	// set the operation id start from 1 again
	lastop = 0;
	unlink(LOGFILE);
}

// handle request from other server and coordinator 
void loop()
{
	int ret;
	struct sockaddr_in addr;

	while (1)
	{
		my_recv(servsock, addr);
		switch (msg.msg_type)
		{
		case HEARTBEAT:
			{
				my_send(servsock, addr);
				break;
			}
		case RESYNCH:
			{
				int reqopid = msg.ival[0];
				vector<operation> recs;
				get_log(recs);
				if (recs.size() > 0)
				{
					for (size_t i = 0; i < recs.size(); ++ i)
					{
						if (recs[i].serial <= reqopid)
							continue;
						make_msg(&msg, RESYNCH_RET, recs[i].serial, recs[i].account, recs[i].balance);
						my_send(servsock, addr);
					}
				}
				make_msg(&msg, RESYNCH_RET, -1);	// indicate send end 
				my_send(servsock, addr);
				remove_log();
				break;
			}
		case DEPOSIT: 
			{
				int index = search(msg.ival[0]);
				infos[index].balance += msg.ival[1];
				make_msg(&msg, OP_DONE, msg.ival[0], 1, msg.ival[2]);		// deposit never fail 
				my_send(servsock, addr);
				account_write(infos);
				write_log(msg.ival[0], infos[index].balance);
				break;
			}
		case WITHDRAW: 
			{
				int index = search(msg.ival[0]);
				int result = 1;
				if (infos[index].balance >= msg.ival[1])
				{
					infos[index].balance -= msg.ival[1];
					result = infos[index].balance;
				}
				else 
					result = -1;
				make_msg(&msg, OP_DONE, msg.ival[0], result, msg.ival[2]);
				my_send(servsock, addr);
				account_write(infos);
				if (result)
					write_log(msg.ival[0], infos[index].balance);
				break;
			}
		case CHECK: 
			{
				int index = search(msg.ival[0]);
				make_msg(&msg, OP_DONE, msg.ival[0], infos[index].balance, msg.ival[2]);
				my_send(servsock, addr);
				// no need to write files 
				break;
			}
		default:
			error_exit("Receive unknown request!");
		}
	}
}

int main(int argc, char** argv)
{  
	if (argc != 3)
		error_exit("[Usage]: ./server [SvrCfg] [ServerNo(1-3)]");
		
	// read the configs 
	server_config cfgs[4];
	get_cfg(argv[1], cfgs);
	
	int serverno = atoi(argv[2]) - 1;	// start from 0-2
	for (int i = 0; i < 4; ++ i)
	{
		bzero(&allserv[i], sizeof(allserv[i]));
		allserv[i].sin_family = AF_INET;
		if (i == serverno)	// this one is for current server   
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
	if (bind(servsock, (struct sockaddr *)&allserv[serverno], sizeof(allserv[serverno])) < 0)
		error_exit("Bind failed!"); 
	
	account_read(infos);
	
	// after it started, will send coordinator the message 
	make_msg(&msg, ALIVE);
	my_send(servsock, allserv[3]);

	// try to check own log and if there is, send operation number to other 2 servers 
	vector<operation> recs;
	get_log(recs);
	if (recs.size() > 0)
		lastop = recs[recs.size() - 1].serial;

	// the other 2 servers will response the same result 
	for (int i = 0; i < 3; ++ i)
	{
		if (i == serverno)
			continue;
		make_msg(&msg, RESYNCH, lastop);
		my_send(servsock, allserv[i]);
	}
	
	// use select to wait for all the response 
	while (1)
	{
		struct timeval tm;
		tm.tv_sec = 0;
		tm.tv_usec = 250000;	// 250ms
		fd_set rdfds;
		FD_ZERO(&rdfds);	
		FD_SET(servsock, &rdfds);
		
		int ret = select(servsock + 1, &rdfds, NULL, NULL, &tm);
		if (ret < 0)
			error_exit("Select wrong!");
		if (ret == 0)
			break;
		
		struct sockaddr_in addr;
		my_recv(servsock, addr);
		if (msg.msg_type == RESYNCH_RET)
		{
			if (lastop < msg.ival[0])	// ignore else resync data 
			{
				int index = search(msg.ival[1]);
				infos[index].balance = msg.ival[2];
				lastop = msg.ival[0];
			}
		}
	}
	// send resync-done to the coordinator 
	make_msg(&msg, RESYNCH_DONE);
	my_send(servsock, allserv[3]);

	// clean the log after resynch finished 
	remove_log();
	
	// handle the request by coordinator and other 2 servers 
	loop();

	close(servsock);  
	return 0;  
} 

