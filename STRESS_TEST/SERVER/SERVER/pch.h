#pragma once

#include <iostream>
#include <fstream>
#include <sstream>

#include <array>
#include <vector>
#include <unordered_set>
#include <concurrent_priority_queue.h>
#include <queue>

#include <thread>
#include <mutex>

#include <WS2tcpip.h>
#include <MSWSock.h>

#include "protocol.h"

#include "include/lua.hpp"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")

using namespace std;

constexpr int VIEW_RANGE = 15;
