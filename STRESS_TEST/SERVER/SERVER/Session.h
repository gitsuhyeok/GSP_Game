#pragma once

#include "Over.h"

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

class SESSION {
	OVER_EXP _recv_over;

public:
	//npc, ai, timer
	//bool _is_npc;
	atomic_bool _is_active;
	//chrono::system_clock::time_point _npc_move_time;

	mutex _s_lock;
	S_STATE _state;

	SOCKET _socket;
	char	_name[NAME_SIZE];

	int		_prev_remain;
	int		_last_move_time;
	int		_last_attack_time;

	//session info
	int		visual;
	int		_id;
	int		hp;
	int		max_hp;
	int		exp;
	int		level;
	short	x, y;

	int atk; //공격력

	//시야처리
	unordered_set <int> _view_list;
	mutex	_vl;

	//섹터
	int sec_id;

	//루아스크립트
	lua_State* _L;
	mutex	_ll;

	//
	int awake_count;

public:
	SESSION();
	~SESSION() {}

	void do_recv();
	void do_send(void* packet);

	void send_login_info_packet();
	void send_move_packet(int c_id);
	void send_add_player_packet(int c_id);
	void send_chat_packet(int c_id, const char* mess);
	void send_remove_player_packet(int c_id);
	void send_change_stat();

	int damaged(int dam);
	void check_now_level();
};

extern array<SESSION, MAX_USER + MAX_NPC> clients;

bool is_pc(int object_id);
bool is_npc(int object_id);
bool can_see(int a, int b);
int get_new_client_id();




// lua script
int API_get_x(lua_State* L);
int API_get_y(lua_State* L);
int API_SendMessage(lua_State* L);

