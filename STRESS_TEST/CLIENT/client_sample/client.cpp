#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
using namespace std;

#include "..\..\SERVER\SERVER\protocol.h"

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 16;
constexpr auto SCREEN_HEIGHT = 16;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = SCREEN_WIDTH * TILE_WIDTH;   // size of window
constexpr auto WINDOW_HEIGHT = SCREEN_WIDTH * TILE_WIDTH;

int g_left_x;
int g_top_y;
int g_myid;

sf::RenderWindow* g_window;
sf::Font g_font;

sf::Texture* board;
sf::Texture* pieces;

sf::Texture* redlink;
sf::Texture* greenlink;
sf::Texture* bluelink;
sf::Texture* pinklink;

sf::Texture* npc;

sf::Texture* sandmonster;
sf::Texture* skulmonster;
sf::Texture* bossmonster;

sf::Texture* maptile;

//enum OBJECTTYPE {
//	REDLINK, GREENLINK, BLUELINK, PINKLINK, //0 1 2 3
//	NPC_SKY, NPC_RED, NPC_BLUE, NPC_GREEN, NPC_PINK, NPC_YELLOW, //4 5 6 7 8 9
//	SANDMONSTER, SKULMONSTER, BOSSMONSTER,
//	GRASS1, GRASS2, GRASS3,
//	GREEN1, GREEN2,
//	FLOWER1, FLOWER2,
//	GRASSBLOCK, SANDBLOCK, WOODBLOCK,
//	SAND1, SAND2, SAND3, SAND4
//};
enum MOVEDIRECTION {UP, DOWN, LEFT, RIGHT};

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;

	//OBJECTTYPE obj_type;

	sf::Text m_name;
	sf::Text m_chat;
	chrono::system_clock::time_point m_mess_end_time;
public:
	int id;
	int m_x, m_y;
	char name[NAME_SIZE];
	int direction; //0:up, 1:down, 2:left, 3:right

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;

		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		m_sprite.setScale(64.f / x2, 64.f / y2);

		set_name("NONAME");
		m_mess_end_time = chrono::system_clock::now();
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 1;
		float ry = (m_y - g_top_y) * 65.0f + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		auto size = m_name.getGlobalBounds();
		if (m_mess_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_chat);
		}
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		if (id < MAX_USER) m_name.setFillColor(sf::Color(255, 255, 255));
		else m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}

	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}
};

OBJECT avatar;							//주인공
unordered_map <int, OBJECT> players;	//다른 플레이어, NPC

OBJECT white_tile;
OBJECT black_tile;

//기본 상하좌우 보유  4개슬롯 + 맵타일은 15개슬롯
//2가지 yellow blue
//5가지 //2가지 grass, sand //4가지 

OBJECT GRASS1; OBJECT GRASS2; OBJECT GRASS3;
OBJECT GREEN1; OBJECT GREEN2;
OBJECT FLOWER1; OBJECT FLOWER2;
OBJECT GRASSBLOCK; OBJECT SANDBLOCK; OBJECT WOODBLOCK;
OBJECT SAND1; OBJECT SAND2; OBJECT SAND3; OBJECT SAND4;

void change_ahlpa(sf::Texture *image,int r, int g, int b)
{
	sf::Image img = image->copyToImage();
	sf::Color backgroundColor(r, g, b);
	for (unsigned int i = 0; i < img.getSize().x; ++i)
	{
		for (unsigned int j = 0; j < img.getSize().y; ++j) {
			if (img.getPixel(i, j) == backgroundColor) {
				img.setPixel(i, j, sf::Color(r, g, b, 0)); // Set to transparent
			}
		}
	}
	image->loadFromImage(img);
}

void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;

	redlink = new sf::Texture;
	greenlink = new sf::Texture;
	bluelink = new sf::Texture;
	pinklink = new sf::Texture;

	npc = new sf::Texture;

	sandmonster = new sf::Texture;
	skulmonster = new sf::Texture;
	bossmonster = new sf::Texture;

	maptile = new sf::Texture;

	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("chess2.png");

	redlink->loadFromFile("RedLink.png");
	greenlink->loadFromFile("GreenLink.png");
	bluelink->loadFromFile("BlueLink.png");
	pinklink->loadFromFile("PinkLink.png");
	
	npc->loadFromFile("NPC.png");
	
	sandmonster->loadFromFile("SandMonster.png");
	skulmonster->loadFromFile("SkulMonster.png");
	bossmonster->loadFromFile("Boss.png");
	
	maptile->loadFromFile("Map.png");

	//텍스처 배경 투명화
	change_ahlpa(redlink, 255, 183, 185);
	change_ahlpa(greenlink, 116, 228, 150);
	change_ahlpa(bluelink, 188, 231, 241);
	change_ahlpa(pinklink, 235, 197, 252);

	change_ahlpa(npc, 234, 187, 45);

	change_ahlpa(sandmonster, 76, 94, 255);
	change_ahlpa(skulmonster, 255, 233, 127);

	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };

	avatar = OBJECT{ *redlink, 37, 116, 18, 35 }; //캐릭터 선택을 만들거?

	GRASS1 = OBJECT{ *maptile, 84, 44, 16, 16 };
	GRASS2 = OBJECT{ *maptile, 104, 44, 16, 16 };
	GRASS3 = OBJECT{ *maptile, 124, 44, 16, 16 };
	GREEN1 = OBJECT{ *maptile, 24, 24, 16, 16 };
	GREEN2 = OBJECT{ *maptile, 44, 24, 16, 16 };
	FLOWER1 = OBJECT{ *maptile, 144, 44, 16, 16 };
	FLOWER2 = OBJECT{ *maptile, 164, 44, 16, 16 };
	GRASSBLOCK = OBJECT{ *maptile, 224, 24, 16, 16 };
	SANDBLOCK = OBJECT{ *maptile, 424, 24, 16, 16 };
	WOODBLOCK = OBJECT{ *maptile, 344, 24, 16, 16 };
	SAND1 = OBJECT{ *maptile, 164, 64, 16, 16 };
	SAND2 = OBJECT{ *maptile, 184, 64, 16, 16 };
	SAND3 = OBJECT{ *maptile, 164, 84, 16, 16 };
	SAND4 = OBJECT{ *maptile, 184, 84, 16, 16 };

	avatar.move(4, 4);
}

void client_finish()
{
	players.clear();
	delete board;
	delete pieces;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	cout << "now packet type <<" << (int)ptr[2] << endl;;
	switch (ptr[2])
	{
	case SC_LOGIN_INFO:
	{
		SC_LOGIN_INFO_PACKET* packet = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(ptr);
		g_myid = packet->id;
		avatar.id = g_myid;
		avatar.move(packet->x, packet->y);
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		avatar.show();
	}
	break;

	case SC_ADD_OBJECT:
	{
		SC_ADD_OBJECT_PACKET* my_packet = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(ptr);
		int id = my_packet->id;

		if (id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
			avatar.show();
		}
		else if (id < MAX_USER) {
			players[id] = OBJECT{ *redlink, 37, 116, 18, 35 };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		else {
			players[id] = OBJECT{ *sandmonster, 9, 88, 40, 32 };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		break;
	}
	case SC_MOVE_OBJECT:
	{
		SC_MOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case SC_REMOVE_OBJECT:
	{
		SC_REMOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {
			players.erase(other_id);
		}
		break;
	}
	case SC_CHAT:
	{
		SC_CHAT_PACKET* my_packet = reinterpret_cast<SC_CHAT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.set_chat(my_packet->mess);
		}
		else {
			//cout << "error" << endl;
			players[other_id].set_chat(my_packet->mess);
		}

		break;
	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[2]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size)
		{
			in_packet_size = *reinterpret_cast<unsigned short*>(ptr);
			//ptr += sizeof(unsigned short);
		}
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		exit(-1);
	}
	if (recv_result == sf::Socket::Disconnected) {
		wcout << L"Disconnected\n";
		exit(-1);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (0 == (tile_x / 3 + tile_y / 3) % 2) {
				GRASS1.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				GRASS1.a_draw();
			}
			else
			{
				SAND1.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				SAND1.a_draw();
			}
		}
	avatar.draw();
	for (auto& pl : players) pl.second.draw();
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	text.setString(buf);
	g_window->draw(text);
}

void send_packet(void* packet)
{
	unsigned char* p = reinterpret_cast<unsigned char*>(packet);
	size_t sent = 0;
	s_socket.send(packet, p[0], sent);
}

int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = s_socket.connect("127.0.0.1", PORT_NUM);
	s_socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		exit(-1);
	}

	client_initialize();
	CS_LOGIN_PACKET p;
	p.size = sizeof(p);
	p.type = CS_LOGIN;

	string player_name{ "P" };
	player_name += to_string(GetCurrentProcessId());

	strcpy_s(p.name, player_name.c_str());
	send_packet(&p);
	avatar.set_name(p.name);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				int direction = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					direction = 2;
					break;
				case sf::Keyboard::Right:
					direction = 3;
					break;
				case sf::Keyboard::Up:
					direction = 0;
					break;
				case sf::Keyboard::Down:
					direction = 1;
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				}
				if (-1 != direction) {
					CS_MOVE_PACKET p;
					p.size = sizeof(p);
					p.type = CS_MOVE;
					p.direction = direction;
					send_packet(&p);
				}

			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}