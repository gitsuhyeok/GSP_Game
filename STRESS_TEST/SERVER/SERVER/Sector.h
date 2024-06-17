#pragma once
class Sector
{
public:
	//하나의 섹터 크기는 100으로 총 400개의 섹터
	int _sector_id;

	mutex _sector;
	unordered_set<int> _obj_id;
public:
	Sector(){}
	Sector(int id) : _sector_id(id)
	{
	}
	~Sector() {}

	void AddObject(int c_id);
	void DelObject(int c_id);

};

extern array<Sector*, 400> g_Sector;

void InitializeSector();
int InitSector(int c_id, int x, int y);
void Insert_sector(int sec_id, int c_id);
void Delete_sector(int sec_id, int c_id);
int Update_sector(int c_id, int x, int y, int c_sec_id);