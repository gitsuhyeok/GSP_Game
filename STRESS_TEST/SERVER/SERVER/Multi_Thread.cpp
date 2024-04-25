#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include "protocol.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
using namespace std;

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L"에러" << lpMsgBuf << std::endl;

	LocalFree(lpMsgBuf);
	// while (true);
}



enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND };
class OVER_EXP {
public:
	WSAOVERLAPPED _over;
	WSABUF _wsabuf;
	char _send_buf[BUF_SIZE];
	COMP_TYPE _comp_type;
	OVER_EXP()
	{
		_wsabuf.len = BUF_SIZE;
		_wsabuf.buf = _send_buf;
		_comp_type = OP_RECV;
		ZeroMemory(&_over, sizeof(_over));
	}
	OVER_EXP(char* packet)
	{
		_wsabuf.len = packet[0];
		_wsabuf.buf = _send_buf;
		ZeroMemory(&_over, sizeof(_over));
		_comp_type = OP_SEND;
		memcpy(_send_buf, packet, packet[0]);
	}
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };
class SESSION {
public:
	OVER_EXP _recv_over;
	DWORD _recv_bytes;

	S_STATE _state;
	bool _is_active;
	int _id;
	SOCKET _socket;
	short	x, y;
	char	_name[NAME_SIZE];
	int		_prev_remain;
	int		_last_move_time;
public:
	SESSION()
	{
		_id = -1;
		_socket = 0;
		x = y = 0;
		_name[0] = 0;
		_state = ST_FREE;
		_is_active = false;
		_prev_remain = 0;
	}

	~SESSION() {}

	int do_recv()
	{
		DWORD recv_flag = 0;
		memset(&_recv_over._over, 0, sizeof(_recv_over._over));
		_recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
		_recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
		return WSARecv(_socket, &_recv_over._wsabuf, 1, &_recv_bytes, &recv_flag,
			0, 0);
	}

	void do_send(void* packet)
	{
		OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
		int ret = WSASend(_socket, &sdata->_wsabuf, 1, NULL, 0, NULL, NULL);
		if (0 != ret) {
			if (::WSAGetLastError() == WSAEWOULDBLOCK)
				return;
		}
	}
	void send_login_info_packet()
	{
		SC_LOGIN_INFO_PACKET p;
		p.id = _id;
		p.size = sizeof(SC_LOGIN_INFO_PACKET);
		p.type = SC_LOGIN_INFO;
		p.x = x;
		p.y = y;
		do_send(&p);
	}
	void send_move_packet(int c_id);
	void send_add_player_packet(int c_id);
	void send_remove_player_packet(int c_id)
	{
		SC_REMOVE_PLAYER_PACKET p;
		p.id = c_id;
		p.size = sizeof(p);
		p.type = SC_REMOVE_PLAYER;
		do_send(&p);
	}
};

array<SESSION, MAX_USER> clients;

SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

void SESSION::send_move_packet(int c_id)
{
	SC_MOVE_PLAYER_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_MOVE_PLAYER_PACKET);
	p.type = SC_MOVE_PLAYER;
	p.x = clients[c_id].x;
	p.y = clients[c_id].y;
	p.move_time = clients[c_id]._last_move_time;
	do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
	SC_ADD_PLAYER_PACKET add_packet;
	add_packet.id = c_id;
	strcpy_s(add_packet.name, clients[c_id]._name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_PLAYER;
	add_packet.x = clients[c_id].x;
	add_packet.y = clients[c_id].y;
	do_send(&add_packet);
}

int get_new_client_id()
{
	for (int i = 0; i < MAX_USER; ++i) {
		if (clients[i]._state == ST_FREE)
			return i;
	}
	return -1;
}

void process_packet(int c_id, char* packet)
{
	switch (packet[1]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		strcpy_s(clients[c_id]._name, p->name);
		clients[c_id].send_login_info_packet();
		for (auto& pl : clients) {
			if (ST_INGAME != pl._state) continue;
			if (pl._id == c_id) continue;
			pl.send_add_player_packet(c_id);
			clients[c_id].send_add_player_packet(pl._id);
		}
		break;
	}
	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		clients[c_id]._last_move_time = p->move_time;
		short x = clients[c_id].x;
		short y = clients[c_id].y;
		switch (p->direction) {
		case 0: if (y > 0) y--; break;
		case 1: if (y < W_HEIGHT - 1) y++; break;
		case 2: if (x > 0) x--; break;
		case 3: if (x < W_WIDTH - 1) x++; break;
		}
		clients[c_id].x = x;
		clients[c_id].y = y;

		for (auto& cl : clients) {
			if (cl._state != ST_INGAME) continue;
			cl.send_move_packet(c_id);

		}
	}
	}
}

void disconnect(int c_id)
{
	for (auto& pl : clients) {
		if (ST_INGAME != pl._state) continue;
		if (pl._id == c_id) continue;
		pl.send_remove_player_packet(c_id);
	}
	closesocket(clients[c_id]._socket);
	clients[c_id]._state = ST_FREE;
}

void worker_thread(SESSION cl)
{
	while (true) {
		if (cl._state != ST_INGAME) continue;
		int recv_return = cl.do_recv();
		int recv_bytes = cl._recv_bytes;
		if (recv_return == SOCKET_ERROR)
		{
			if (::WSAGetLastError() == WSAEWOULDBLOCK)
				continue;
			//에러

		}
		else if (recv_bytes == 0)
		{
			//연결 끊김
			disconnect(cl._id);
			continue;
		}

		//recv 성공
		int remain_data = recv_bytes + cl._prev_remain;
		char* p = cl._recv_over._send_buf;
		while (remain_data > 0)
		{
			int packet_size = p[0];
			if (packet_size <= remain_data)
			{
				process_packet(cl._id, p);
				p = p + packet_size;
				remain_data = remain_data - packet_size;
			}
			else break;
		}
		cl._prev_remain = remain_data;
		if (remain_data > 0) {
			memcpy(cl._recv_over._send_buf, p, remain_data);
		}
	}
}


int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, 0);

	//논블로킹 소켓
	unsigned long noblock = 1;
	int nRet = ioctlsocket(server, FIONBIO, &noblock);

	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(server, SOMAXCONN);


	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);

	vector <thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();

	while (true) {
		SOCKET client = WSAAccept(server,
			reinterpret_cast<sockaddr*>(&cl_addr), &addr_size, NULL, NULL);
		if (client != INVALID_SOCKET)
		{
			int client_id = get_new_client_id();
			if (client_id != -1) {
				clients[client_id]._state = ST_INGAME;
				clients[client_id].x = 0;
				clients[client_id].y = 0;
				clients[client_id]._id = client_id;
				clients[client_id]._name[0] = 0;
				clients[client_id]._prev_remain = 0;
				clients[client_id]._socket = client;

				worker_threads.emplace_back(worker_thread, clients[client_id]);
			}
			else {
				cout << "Max user exceeded.\n";
			}
		}
	}

	for (auto& th : worker_threads)
		th.join();

	closesocket(server);
	WSACleanup();
}