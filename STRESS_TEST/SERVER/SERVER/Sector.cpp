#include "pch.h"
#include "Sector.h"

array<Sector*, 625> g_Sector;

void Sector::AddObject(int c_id)
{
	_sector.lock();
	_obj_id.emplace(c_id);
	_sector.unlock();
}

void Sector::DelObject(int c_id)
{
	_sector.lock();
	_obj_id.erase(c_id);
	_sector.unlock();
}


void InitializeSector()
{
	cout << "Sector initialize begin.\n";
	for (int i = 0; i <= 625; i++)
	{
		Sector* temp = new Sector(i);
		g_Sector[i] = temp;
	}
	cout << "Sector initialize end.\n";
}

int InitSector(int c_id, int x, int y)
{
	int height = y / 80;
	int width = x / 80;
	int sec_id = height * 25 + width;
	Insert_sector(sec_id, c_id);
	return sec_id;
}
void Insert_sector(int sec_id, int c_id)
{
	g_Sector[sec_id]->AddObject(c_id);
}
void Delete_sector(int sec_id, int c_id)
{
	g_Sector[sec_id]->DelObject(c_id);
}
int Update_sector(int c_id, int x, int y, int c_sec_id)
{
	int height = y / 80;
	int width = x / 80;
	int sec_id = height * 25 + width;

	if (sec_id != c_sec_id)
	{
		Delete_sector(sec_id, c_id);
		Insert_sector(sec_id, c_id);
	}

	return sec_id;
}

