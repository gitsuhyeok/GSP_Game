#pragma once

enum EVENT_TYPE { EV_RANDOM_MOVE, EV_CHASE, EV_HEAL, EV_ATTACK, EV_AUTOHEAL };

class TIMER_EVENT {
public:
	int obj_id;
	chrono::system_clock::time_point wakeup_time;
	EVENT_TYPE event_id;
	int target_id;

	constexpr bool operator < (const TIMER_EVENT& L) const
	{
		return (wakeup_time > L.wakeup_time);
	}

};