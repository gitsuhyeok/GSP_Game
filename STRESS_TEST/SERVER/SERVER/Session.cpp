#include "pch.h"
#include "Session.h"

array<SESSION, MAX_USER + MAX_NPC> clients;

SESSION::SESSION()
{
	_id = -1;
	_socket = 0;
	x = y = 0;
	_name[0] = 0;
	_state = ST_FREE;
	_prev_remain = 0;

	//
	awake_count = -1;
}

void SESSION::do_recv()
{
	DWORD recv_flag = 0;
	memset(&_recv_over._over, 0, sizeof(_recv_over._over));
	_recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
	_recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
	WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag,
		&_recv_over._over, 0);
}

void SESSION::do_send(void* packet)
{
	OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
	WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
}

void SESSION::send_login_info_packet()
{
	SC_LOGIN_INFO_PACKET p;
	p.size = sizeof(SC_LOGIN_INFO_PACKET);
	p.type = SC_LOGIN_INFO;
	p.visual = visual;
	p.id = _id;
	p.hp = hp;
	p.max_hp = max_hp;
	p.exp = exp;
	p.level = level;
	p.x = x;
	p.y = y;
	do_send(&p);
}

void SESSION::send_move_packet(int c_id)
{
	SC_MOVE_OBJECT_PACKET p;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.id = c_id;
	p.x = clients[c_id].x;
	p.y = clients[c_id].y;
	p.move_time = clients[c_id]._last_move_time;
	do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
	SC_ADD_OBJECT_PACKET add_packet;
	strcpy_s(add_packet.name, clients[c_id]._name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_OBJECT;
	add_packet.id = c_id;
	add_packet.visual = clients[c_id].visual;
	add_packet.x = clients[c_id].x;
	add_packet.y = clients[c_id].y;
	_vl.lock();
	_view_list.insert(c_id);
	_vl.unlock();

	do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess)
{
	SC_CHAT_PACKET packet;
	packet.id = p_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	strcpy_s(packet.mess, mess);
	do_send(&packet);
}

void SESSION::send_remove_player_packet(int c_id)
{
	_vl.lock();
	if (_view_list.count(c_id))
		_view_list.erase(c_id);
	else {
		_vl.unlock();
		return;
	}
	_vl.unlock();

	SC_REMOVE_OBJECT_PACKET p;
	p.size = sizeof(p);
	p.type = SC_REMOVE_OBJECT;
	p.id = c_id;
	do_send(&p);
}

void SESSION::send_change_stat()
{
	SC_STAT_CHANGE_PACKET p;
	p.size = sizeof(SC_STAT_CHANGE_PACKET);
	p.type = SC_STAT_CHANGE;
	p.hp = hp;
	p.max_hp = max_hp;
	p.exp = exp;
	p.level = level;
	do_send(&p);
}

int SESSION::damaged(int dam)
{
	cout << _name << "가 " << dam << "의 피해를 입었습니다." << endl;
	hp -= dam;
	cout << "남은 체력 : " << hp << endl;
	if (hp < 0)
	{
		hp = 0;
		return level * level * 20;
	}
	return 0;
}

void SESSION::check_now_level()
{
	if (exp >= level * level * 100)
	{
		level += 1;
		exp = 0;
		cout << _name << "의 레벨이 올랐습니다. 현재 레벨 : " << level << endl;
		send_change_stat();
	}
}



bool is_pc(int object_id)
{
	return object_id < MAX_USER;
}
bool is_npc(int object_id)
{
	return !is_pc(object_id);
}
bool can_see(int a, int b)
{

	// int dist = sqrtf((clients[a].x - clients[b].x) * (clients[a].x - clients[b].x)
	//	+ (clients[a].y - clients[b].y) * (clients[a].y - clients[b].y));
	int dist_s = (clients[a].x - clients[b].x) * (clients[a].x - clients[b].x)
		+ (clients[a].y - clients[b].y) * (clients[a].y - clients[b].y);

	return VIEW_RANGE * VIEW_RANGE >= dist_s;

	//if (abs(clients[a].x - clients[b].x) > VIEW_RANGE) return false;
	//return abs(clients[a].y - clients[b].y) <= VIEW_RANGE;
}
int get_new_client_id()
{
	for (int i = 0; i < MAX_USER; ++i)
	{
		lock_guard <mutex> ll{ clients[i]._s_lock };
		if (clients[i]._state == ST_FREE)
			return i;
	}
	return -1;
}
 


// lua script
int API_get_x(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	int x = clients[user_id].x;
	lua_pushnumber(L, x);
	return 1;
}

int API_get_y(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	int y = clients[user_id].y;
	lua_pushnumber(L, y);
	return 1;
}

int API_SendMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int user_id = (int)lua_tointeger(L, -2);
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 4);

	//clients[user_id].send_chat_packet(my_id, mess);
	return 0;
}
