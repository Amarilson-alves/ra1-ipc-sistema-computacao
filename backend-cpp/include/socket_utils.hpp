#ifndef SOCKET_UTILS_HPP
#define SOCKET_UTILS_HPP

// Define isso ANTES de incluir windows.h para evitar conflitos
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

// Link com a biblioteca Winsock
#pragma comment(lib, "ws2_32.lib")

#endif // SOCKET_UTILS_HPP