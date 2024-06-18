#include "pch.h"
#include "Over.h"
#include "Session.h"
#include "Timer.h"
#include "Sector.h"

concurrency::concurrent_priority_queue<TIMER_EVENT> timer_queue;
vector<vector<int>> level_vec(W_WIDTH, vector<int>(W_HEIGHT));
mutex _level;

HANDLE h_iocp;

//AcceptEx
SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

void WakeUpNPC(int npc_id, int waker)
{
	OVER_EXP* exover = new OVER_EXP;
	exover->_comp_type = OP_AI_HELLO;
	exover->_ai_target_obj = waker;
	PostQueuedCompletionStatus(h_iocp, 1, npc_id, &exover->_over);

	if (clients[npc_id]._is_active) return;
	bool old_state = false;
	if (false == atomic_compare_exchange_strong(&clients[npc_id]._is_active, &old_state, true))
		return;
	TIMER_EVENT ev{ npc_id, chrono::system_clock::now(), EV_RANDOM_MOVE, waker };
	timer_queue.push(ev);
}


bool BlockCheck(int x, int y)
{
	_level.lock();
	if (level_vec[x][y] == 7 || level_vec[x][y] == 8 || level_vec[x][y] == 9)
	{
		_level.unlock();
		return true;
	}
	_level.unlock();
	return false;
}

void process_packet(int c_id, char* packet)
{
	switch (packet[2]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);

		char dataname[20];
		strcpy_s(dataname, p->name);
		string data(dataname);
		size_t pos = data.find(":");
		string player_name = data.substr(0, pos);
		unsigned int color = std::stoul(data.substr(pos + 1));

		strcpy_s(clients[c_id]._name, player_name.c_str());

		{
			lock_guard<mutex> ll{ clients[c_id]._s_lock };
			clients[c_id]._state = ST_INGAME;
			clients[c_id].x = 850;
			clients[c_id].y = 851;
			clients[c_id].visual = color;
		}

		clients[c_id].send_login_info_packet();

		//생성 sector insert
		clients[c_id].sec_id = InitSector(c_id, clients[c_id].x, clients[c_id].y);
		//

		g_Sector[clients[c_id].sec_id]->_sector.lock();
		unordered_set<int> sector = g_Sector[clients[c_id].sec_id]->_obj_id;
		g_Sector[clients[c_id].sec_id]->_sector.unlock();

		for (auto& sc : sector)
		{
			{
				lock_guard<mutex> ll(clients[sc]._s_lock);
				if (ST_INGAME != clients[sc]._state) continue;
			}
			if (sc == c_id) continue;
			if (false == can_see(sc, c_id))
				continue;
			if (is_pc(clients[sc]._id))
				clients[sc].send_add_player_packet(c_id);
			else
				WakeUpNPC(clients[sc]._id, c_id);
			clients[c_id].send_add_player_packet(sc);
		}

		TIMER_EVENT ev{ c_id, chrono::system_clock::now(), EV_AUTOHEAL, c_id };
		timer_queue.push(ev);

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

		g_Sector[clients[c_id].sec_id]->_sector.lock();
		unordered_set<int> sector = g_Sector[clients[c_id].sec_id]->_obj_id;
		g_Sector[clients[c_id].sec_id]->_sector.unlock();

		for (auto& sc : sector)
		{
			if (clients[sc].x == x && clients[sc].y == y)
				return;
		}

		if (BlockCheck(x, y)) return;

		clients[c_id].x = x;
		clients[c_id].y = y;
		clients[c_id].send_move_packet(c_id);

		//이동 sector update
		clients[c_id].sec_id = Update_sector(c_id, clients[c_id].x, clients[c_id].y, clients[c_id].sec_id);
		//

		g_Sector[clients[c_id].sec_id]->_sector.lock();
		sector = g_Sector[clients[c_id].sec_id]->_obj_id;
		g_Sector[clients[c_id].sec_id]->_sector.unlock();

		unordered_set<int> near_list;
		clients[c_id]._vl.lock();
		unordered_set<int> old_vl = clients[c_id]._view_list;
		clients[c_id]._vl.unlock();

		for (auto& sc : sector)
		{
			if (clients[sc]._state != ST_INGAME) continue;
			if (sc == c_id) continue;
			if (true == can_see(sc, c_id))
			{
				near_list.insert(sc);
				//if (true == objects[sc]._is_npc && false == objects[sc]._is_active) {
				//	bool input = false;
				//	if (true == atomic_compare_exchange_strong(&objects[sc]._is_active, &input, true))
				//		add_timer(objects[sc]._id, EV_RANDOM_MOVE, 1000);
				//}
			}
		}


		// ADD_PLAYER
		for (auto& cl : near_list) {
			auto& cpl = clients[cl];
			if (is_pc(cl))
			{
				cpl._vl.lock();
				if (clients[cl]._view_list.count(c_id))
				{
					cpl._vl.unlock();
					clients[cl].send_move_packet(c_id);
				}
				else
				{
					cpl._vl.unlock();
					clients[cl].send_add_player_packet(c_id);
				}
			}
			else
				WakeUpNPC(cl, c_id);

			if (old_vl.count(cl) == 0)
				clients[c_id].send_add_player_packet(cl);
		}
		// REMOVE_PLAYER
		for (auto& cl : old_vl)
		{
			if (0 == near_list.count(cl))
			{
				clients[c_id].send_remove_player_packet(cl);
				if (is_pc(cl))
					clients[cl].send_remove_player_packet(c_id);
			}
		}
	}
		break;

	case CS_ATTACK: {
		CS_ATTACK_PACKET* p = reinterpret_cast<CS_ATTACK_PACKET*>(packet);
		clients[c_id]._last_attack_time = p->attack_time;

		g_Sector[clients[c_id].sec_id]->_sector.lock();
		unordered_set<int> sector = g_Sector[clients[c_id].sec_id]->_obj_id;
		g_Sector[clients[c_id].sec_id]->_sector.unlock();

		if (p->key == 1)
		{
			vector<vector<int>> attackedtile;

			int c_x = clients[c_id].x;
			int c_y = clients[c_id].y;
			switch (clients[c_id].visual) {
			case 1: //r sword
				switch (p->direction) {
				case 1:
					attackedtile.emplace_back(vector<int>{c_x - 2, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x + 2, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y + 2});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 2});
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y + 2});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 3});
					break;
				case 2:
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y - 2});
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y});
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y + 2});
					attackedtile.emplace_back(vector<int>{c_x - 2, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x - 2, c_y});
					attackedtile.emplace_back(vector<int>{c_x - 2, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x - 3, c_y});
					break;
				case 3:
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y - 2});
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y});
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y + 2});
					attackedtile.emplace_back(vector<int>{c_x + 2, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x + 2, c_y});
					attackedtile.emplace_back(vector<int>{c_x + 2, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x + 3, c_y});
					break;
				case 4:
					attackedtile.emplace_back(vector<int>{c_x - 2, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x + 2, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y - 2});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 2});
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y - 2});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 3});
					break;
				}
				break;
			case 2: //g bow
				switch (p->direction){
				case 1:
					attackedtile.emplace_back(vector<int>{c_x, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 2});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 3});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 4});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 5});
					break;
				case 2:
					attackedtile.emplace_back(vector<int>{c_x-1, c_y});
					attackedtile.emplace_back(vector<int>{c_x-2, c_y});
					attackedtile.emplace_back(vector<int>{c_x-3, c_y});
					attackedtile.emplace_back(vector<int>{c_x-4, c_y});
					attackedtile.emplace_back(vector<int>{c_x-5, c_y});
					break;
				case 3:
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y});
					attackedtile.emplace_back(vector<int>{c_x + 2, c_y});
					attackedtile.emplace_back(vector<int>{c_x + 3, c_y});
					attackedtile.emplace_back(vector<int>{c_x + 4, c_y});
					attackedtile.emplace_back(vector<int>{c_x + 5, c_y});
					break;
				case 4:
					attackedtile.emplace_back(vector<int>{c_x, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 2});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 3});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 4});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 5});
					break;
				}
				break;
			case 3: //b shield
				attackedtile.emplace_back(vector<int>{c_x - 1, c_y - 1});
				attackedtile.emplace_back(vector<int>{c_x - 1, c_y});
				attackedtile.emplace_back(vector<int>{c_x - 1, c_y + 1});
				attackedtile.emplace_back(vector<int>{c_x, c_y + 1});
				attackedtile.emplace_back(vector<int>{c_x + 1, c_y + 1});
				attackedtile.emplace_back(vector<int>{c_x + 1, c_y});
				attackedtile.emplace_back(vector<int>{c_x + 1, c_y - 1});
				attackedtile.emplace_back(vector<int>{c_x, c_y - 1});
				break;
			case 4: //p magic
				switch (p->direction) {
				case 1:
					attackedtile.emplace_back(vector<int>{c_x, c_y + 1});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 2});
					attackedtile.emplace_back(vector<int>{c_x, c_y + 3});
					break;
				case 2:
					attackedtile.emplace_back(vector<int>{c_x - 1, c_y});
					attackedtile.emplace_back(vector<int>{c_x - 2, c_y});
					attackedtile.emplace_back(vector<int>{c_x - 3, c_y});
					break;
				case 3:
					attackedtile.emplace_back(vector<int>{c_x + 1, c_y});
					attackedtile.emplace_back(vector<int>{c_x + 2, c_y});
					attackedtile.emplace_back(vector<int>{c_x + 3, c_y});
					break;
				case 4:
					attackedtile.emplace_back(vector<int>{c_x, c_y - 1});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 2});
					attackedtile.emplace_back(vector<int>{c_x, c_y - 3});
					break;
				}
				break;
			}
			//위 공격 범위 

			//공격 범위에 있는 npc 데미지
			for (auto& sc : sector)
			{
				if (clients[sc]._state != ST_INGAME) continue;
				if (sc == c_id) continue;
				if (is_pc(sc)) continue;
				if (true == can_see(sc, c_id))
				{
					for (auto& at : attackedtile)
					{
						if (clients[sc].x == at[0] && clients[sc].y == at[1])
						{
							int result = clients[sc].damaged(clients[c_id].atk);
							if (result != 0)
							{
								clients[c_id].exp += result;
								clients[c_id].check_now_level();
							}
						}
					}
				}
			}
		}
		else
		{

		}
	}
		break;
	}
}

void disconnect(int c_id)
{
	clients[c_id]._vl.lock();
	unordered_set <int> vl = clients[c_id]._view_list;
	clients[c_id]._vl.unlock();

	for (auto& p_id : vl)
	{
		if (is_npc(p_id))
			continue;
		auto& pl = clients[p_id];
		{
			lock_guard<mutex> ll(pl._s_lock);
			if (ST_INGAME != pl._state) continue;
		}
		if (pl._id == c_id)
			continue;
		pl.send_remove_player_packet(c_id);
	}

	//삭제 sector delete
	Delete_sector(clients[c_id].sec_id, c_id);
	//

	closesocket(clients[c_id]._socket);

	lock_guard<mutex> ll(clients[c_id]._s_lock);
	clients[c_id]._state = ST_FREE;
}

//bool need_act(int npc_id)
//{
//	for (int i = 0; i < MAX_USER; ++i)
//	{
//		if (objects[i].now_sy == objects[npc_id].now_sy &&
//			objects[i].now_sx == objects[npc_id].now_sx &&
//			true == can_see(npc_id, i))
//		{
//			return true;
//		}
//	}
//	return false;
//}

void do_npc_random_move(int npc_id)
{
	SESSION& npc = clients[npc_id];

	g_Sector[clients[npc_id].sec_id]->_sector.lock();
	unordered_set<int> sector = g_Sector[clients[npc_id].sec_id]->_obj_id;
	g_Sector[clients[npc_id].sec_id]->_sector.unlock();

	unordered_set<int> old_vl;
	for (auto& sc : sector)
	{
		if (clients[sc]._state != ST_INGAME) continue;
		if (true == is_npc(clients[sc]._id)) continue;
		if (true == can_see(sc, npc._id))
		{
			old_vl.insert(sc);
		}
	}

	int x = npc.x;
	int y = npc.y;
	switch (rand() % 4) {
	case 0: if (x < (W_WIDTH - 1)) x++; break;
	case 1: if (x > 0) x--; break;
	case 2: if (y < (W_HEIGHT - 1)) y++; break;
	case 3:if (y > 0) y--; break;
	}

	for (auto& sc : sector)
	{
		if (clients[sc].x == x && clients[sc].y == y)
			return;
	}
	if (BlockCheck(x, y)) return;

	npc.x = x;
	npc.y = y;

	//이동 sector update
	npc.sec_id = Update_sector(npc._id, npc.x, npc.y, npc.sec_id);
	//
	g_Sector[clients[npc_id].sec_id]->_sector.lock();
	sector = g_Sector[clients[npc_id].sec_id]->_obj_id;
	g_Sector[clients[npc_id].sec_id]->_sector.unlock();

	unordered_set<int> new_vl;
	for (auto& sc : sector)
	{
		if (clients[sc]._state != ST_INGAME) continue;
		if (true == is_npc(clients[sc]._id)) continue;
		if (true == can_see(sc, npc._id))
		{
			new_vl.insert(sc);
		}
	}

	// ADD_PLAYER
	for (auto& cl : new_vl)
	{
		if (0 == old_vl.count(cl))
		{
			clients[cl].send_add_player_packet(npc._id);
		}
		else
		{
			// MOVE_PLAYER
			clients[cl].send_move_packet(npc._id);
		}
	}
	// REMOVE_PLAYER
	for (auto& cl : old_vl)
	{
		if (0 == new_vl.count(cl))
		{
			clients[cl]._vl.lock();
			if (0 != clients[cl]._view_list.count(npc._id)) {
				clients[cl]._vl.unlock();
				clients[cl].send_remove_player_packet(npc._id);
			}
			else {
				clients[cl]._vl.unlock();
			}
		}
	}
}

void worker_thread(HANDLE h_iocp)
{
	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over = nullptr;
		BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
		OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);
		if (FALSE == ret) {
			if (ex_over->_comp_type == OP_ACCEPT) cout << "Accept Error";
			else {
				cout << "GQCS Error on client[" << key << "]\n";
				disconnect(static_cast<int>(key));
				if (ex_over->_comp_type == OP_SEND) delete ex_over;
				continue;
			}
		}

		if ((0 == num_bytes) && ((ex_over->_comp_type == OP_RECV) || (ex_over->_comp_type == OP_SEND))) {
			disconnect(static_cast<int>(key));
			if (ex_over->_comp_type == OP_SEND) delete ex_over;
			continue;
		}

		switch (ex_over->_comp_type) {
		case OP_ACCEPT: {
			int client_id = get_new_client_id();
			if (client_id != -1) {
				{
					lock_guard<mutex> ll(clients[client_id]._s_lock);
					clients[client_id]._state = ST_ALLOC;
				}
				clients[client_id].x = 0;
				clients[client_id].y = 0;
				clients[client_id]._id = client_id;
				clients[client_id].hp = 100;
				clients[client_id].max_hp = 100;
				clients[client_id].exp = 0;
				clients[client_id].level = 1;
				clients[client_id].atk = 10;
				clients[client_id]._name[0] = 0;
				clients[client_id]._prev_remain = 0;
				clients[client_id]._socket = g_c_socket;
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket),
					h_iocp, client_id, 0);
				clients[client_id].do_recv();
				g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			}
			else {
				cout << "Max user exceeded.\n";
			}
			ZeroMemory(&g_a_over._over, sizeof(g_a_over._over));
			int addr_size = sizeof(SOCKADDR_IN);
			AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);
			break;
		}
		case OP_RECV: {
			int remain_data = num_bytes + clients[key]._prev_remain;
			char* p = ex_over->_send_buf;
			while (remain_data > 0) {
				int packet_size = *reinterpret_cast<unsigned short*>(p);
				if (packet_size <= remain_data) {
					process_packet(static_cast<int>(key), p);
					p = p + packet_size;
					remain_data = remain_data - packet_size;
				}
				else break;
			}
			clients[key]._prev_remain = remain_data;
			if (remain_data > 0) {
				memcpy(ex_over->_send_buf, p, remain_data);
			}
			clients[key].do_recv();
			break;
		}
		case OP_SEND:
			delete ex_over;
			break;
		case OP_NPC_MOVE:{
			bool keep_alive = false;
			for (int j = 0; j < MAX_USER; ++j) {
				if (clients[j]._state != ST_INGAME) continue;
				if (can_see(static_cast<int>(key), j)) {
					keep_alive = true;
					break;
				}
			}
			if (true == keep_alive) {
				do_npc_random_move(static_cast<int>(key));
				clients[key].awake_count -= 1;
				if (clients[key].awake_count == 0)
				{
					clients[key]._ll.lock();
					auto L = clients[key]._L;
					lua_getglobal(L, "event_npc_bye");
					lua_pushnumber(L, ex_over->_ai_target_obj);
					lua_pcall(L, 1, 0, 0);
					lua_pop(L, 1);
					clients[key]._ll.unlock();
				}
				TIMER_EVENT ev{ key, chrono::system_clock::now() + 1s, EV_RANDOM_MOVE, ex_over->_ai_target_obj};
				timer_queue.push(ev);
			}
			else {
				clients[key]._is_active = false;
			}
			delete ex_over;
		}
			break;
		case OP_AI_HELLO: {
			clients[key]._ll.lock();
			auto L = clients[key]._L;
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, ex_over->_ai_target_obj);
			lua_pcall(L, 1, 1, 0);
			int a = lua_tonumber(L,1);
			clients[key].awake_count = a;
			lua_pop(L, 1);
			clients[key]._ll.unlock();

			delete ex_over;
		}
			break;
		case OP_AUTO_HEAL: {
			clients[key].hp += (clients[key].max_hp * 0.1f);
			if (clients[key].hp > clients[key].max_hp)
				clients[key].hp = clients[key].max_hp;

			clients[key].send_change_stat();
			TIMER_EVENT ev{ key, chrono::system_clock::now() + 5s, EV_AUTOHEAL, key };
			timer_queue.push(ev);
		}
			break;
			

		}
	}
}

void InitializeNPC()
{
	std::ifstream mapFile("level.txt");
	if (!mapFile.is_open()) {
		std::cerr << "파일을 열 수 없습니다!" << std::endl;
		return;
	}
	std::string line;
	int x = 0;
	_level.lock();
	while (std::getline(mapFile, line) && x < W_WIDTH) {
		std::istringstream iss(line);
		int y = 0;
		int tile;

		while (iss >> tile && y < W_HEIGHT) {
			level_vec[x][y] = tile;
			++y;
		}
		++x;
	}
	_level.unlock();

	mapFile.close();


	cout << "NPC intialize begin.\n";
	for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
	{
		int randx = 0;
		int randy = 0;

		if (MAX_USER <= i && i < MAX_USER + 30) //마을 NPC
		{
			while (1) {
				randx = rand() % W_WIDTH;
				randy = rand() % W_HEIGHT;

				if (!(randx >= 850 && randx <= 1149 &&
					randy >= 850 && randy <= 1149))
					continue; //마을 위치

				if (!BlockCheck(randx, randy))
					break;
			}
			clients[i].visual = rand() % 2 + 8;
			clients[i]._id = i;
			clients[i].hp = 100;
			clients[i].max_hp = 100;
			clients[i].exp = 0;
			clients[i].level = 1;
			clients[i].x = randx;
			clients[i].y = randy;
			clients[i].atk = 10;
			sprintf_s(clients[i]._name, "FAIRY", i);
			clients[i]._state = ST_INGAME;
		}
		else
		{
			while (1) {
				randx = rand() % W_WIDTH;
				randy = rand() % W_HEIGHT;

				if (randx >= 850 && randx <= 1149 &&
					randy >= 850 && randy <= 1149)
					continue; //마을 위치

				if (!BlockCheck(randx, randy))
					break;
			}
			clients[i].visual = rand() % 3+ 5;
			clients[i]._id = i;
			clients[i].hp = 100;
			clients[i].max_hp = 100;
			clients[i].exp = 0;
			clients[i].level = 1;
			clients[i].x = randx;
			clients[i].y = randy;
			switch (clients[i].visual)
			{
			case 5:
				sprintf_s(clients[i]._name, "SANDMAN", i);
				break;
			case 6:
				sprintf_s(clients[i]._name, "SKUL", i);
				break;
			case 7:
				sprintf_s(clients[i]._name, "BIGEYE", i);
				break;
			}
			clients[i]._state = ST_INGAME;
		}


		//생성 sector insert
		clients[i].sec_id = InitSector(i, clients[i].x, clients[i].y);
		//

		g_Sector[clients[i].sec_id]->_sector.lock();
		unordered_set<int> sector = g_Sector[clients[i].sec_id]->_obj_id;
		g_Sector[clients[i].sec_id]->_sector.unlock();

		for (auto& sc : sector)
		{
			{
				lock_guard<mutex> ll(clients[sc]._s_lock);
				if (ST_INGAME != clients[sc]._state) continue;
			}
			if (sc == i) continue;
			if (false == can_see(sc, i))
				continue;

			if (false == is_npc(sc))
				clients[sc].send_add_player_packet(i);
		}

		auto L = clients[i]._L = luaL_newstate();
		luaL_openlibs(L);
		luaL_loadfile(L, "npc.lua");
		lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 0, 0);
		// lua_pop(L, 1);// eliminate set_uid from stack after call

		lua_register(L, "API_SendMessage", API_SendMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
	}

	cout << "NPC initialize end.\n";
}

void do_timer() //timer 최적화
{
	using namespace chrono;
	while (true)
	{
		TIMER_EVENT ev;
		auto current_time = system_clock::now();

		if (true == timer_queue.try_pop(ev))
		{
			if (ev.wakeup_time > current_time)
			{
				timer_queue.push(ev);
				this_thread::sleep_for(1ms);
				continue;
			}
			switch (ev.event_id)
			{
			case EV_RANDOM_MOVE: {
				OVER_EXP* ov = new OVER_EXP;
				ov->_comp_type = OP_NPC_MOVE;
				ov->_ai_target_obj = ev.target_id;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
			}
				break;
			//case EV_NPC_REVIVE: {

			//}
				break;
			case EV_AUTOHEAL: {
				OVER_EXP* ah = new OVER_EXP;
				ah->_comp_type = OP_AUTO_HEAL;
				ah->_ai_target_obj = ev.target_id;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ah->_over);
			}
				break;
			}
			continue;
		}
		this_thread::sleep_for(1ms);
	}
}


int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_s_socket, SOMAXCONN);
	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);

	InitializeSector();

	InitializeNPC();

	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
	g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_a_over._comp_type = OP_ACCEPT;
	AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

	vector <thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread, h_iocp);
	thread ai_thread{ do_timer };

	for (auto& th : worker_threads)
		th.join();
	ai_thread.join();

	closesocket(g_s_socket);
	WSACleanup();
}
