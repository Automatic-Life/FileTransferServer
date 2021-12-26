#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

std::uint32_t datagrammSize = 65500; 
const int datagrammIDSize = 2;

std::uint32_t getIDfromDatagramm(char* buf, int datagrammDataSize)
{
	std::uint32_t result = 0;
	std::uint32_t mask = 255;
	for (int i = (datagrammDataSize + datagrammIDSize - 1); i > (datagrammDataSize - 1); --i)
	{
		result <<= 8;
		mask &= buf[i];
		result |= mask;
		mask = 255;
	}
	return result;
}

void writeToFile(std::ofstream& file, std::vector<char*>& container, std::uint32_t datagrammDataSize, std::uint32_t start, std::uint32_t end)
{
	for (std::uint32_t i = start; i < end; ++i)
	{
		file.write(container[i], datagrammDataSize);
	}
}

int main(int argc, char* argv[])
{
	const char* IP = argv[1];
	const char* connectionPort = argv[2];
	const std::string downloadDirectory = argv[3];
	
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult)
	{
		std::cout << "WSAStartup failed with error " << iResult;
		exit(1);
	}

	ADDRINFO hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	ADDRINFO* addrResult = nullptr;
	iResult = getaddrinfo(IP, connectionPort, &hints, &addrResult);
	if (iResult)
	{
		std::cout << "getaddrinfo() failed with error " << iResult;
		WSACleanup();
		exit(1);
	}

	SOCKET listenSocket = INVALID_SOCKET;
	listenSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
	if (listenSocket == INVALID_SOCKET)
	{
		std::cout << "Socket creation failed";
		freeaddrinfo(addrResult);
		WSACleanup();
		exit(1);
	}

	iResult = bind(listenSocket, addrResult->ai_addr, (int)addrResult->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		std::cout << "Binding socket failed\n";
		iResult = closesocket(listenSocket);
		if (iResult == SOCKET_ERROR)
		{
			std::cout << "Close socket failed\n";
		}
		freeaddrinfo(addrResult);
		WSACleanup();
		exit(1);
	}

	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		std::cout << "Listening failed\n";
		iResult = closesocket(listenSocket);
		if (iResult == SOCKET_ERROR)
		{
			std::cout << "Close socket failed\n";
		}
		freeaddrinfo(addrResult);
		WSACleanup();
		exit(1);
	}
	
	SOCKET socketTCP = INVALID_SOCKET;
	socketTCP = accept(listenSocket, NULL, NULL);
	if (socketTCP == INVALID_SOCKET)
	{
		std::cout << "Accept failed\n";
		closesocket(listenSocket);
		freeaddrinfo(addrResult);
		WSACleanup();
		exit(1);
	}

	closesocket(listenSocket);
	std::cout << "Client connected\n";

	// Recieving filename and UDP port
	int fileNameSize = 0;
	recv(socketTCP, (char*)&fileNameSize, sizeof(fileNameSize), NULL);
	char* transmissionFileName = new char[fileNameSize];
	recv(socketTCP, transmissionFileName, fileNameSize, NULL);
	
	int UDPPortSize = 0;
	recv(socketTCP, (char*)&UDPPortSize, sizeof(UDPPortSize), NULL);
	char* UDPPort = new char[UDPPortSize];
	recv(socketTCP, UDPPort, UDPPortSize, NULL);
	
	// Creating UDP Socket
	ADDRINFO hintsUDP;
	ZeroMemory(&hintsUDP, sizeof(hintsUDP));
	hintsUDP.ai_family = AF_INET;
	hintsUDP.ai_socktype = SOCK_DGRAM;
	hintsUDP.ai_protocol = IPPROTO_UDP;
	ADDRINFO* addrResultUDP = nullptr;
	iResult = getaddrinfo(IP, UDPPort, &hintsUDP, &addrResultUDP);
	if (iResult)
	{
		std::cout << "getaddrinfo() failed with error " << iResult;
		WSACleanup();
		exit(1);
	}

	SOCKADDR_STORAGE their_addr;
	socklen_t addr_len;
	addr_len = sizeof(their_addr);

	SOCKET socketUDP = INVALID_SOCKET;
	socketUDP = socket(addrResultUDP->ai_family, addrResultUDP->ai_socktype, addrResultUDP->ai_protocol);
	if (socketUDP == INVALID_SOCKET)
	{
		std::cout << "Socket creation failed";
		closesocket(socketTCP);
		closesocket(listenSocket);
		freeaddrinfo(addrResultUDP);
		WSACleanup();
		exit(1);
	}

	iResult = bind(socketUDP, addrResultUDP->ai_addr, (int)addrResultUDP->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		std::cout << "Binding socket failed\n";
		closesocket(socketTCP);
		closesocket(socketUDP);
		closesocket(listenSocket);
		freeaddrinfo(addrResultUDP);
		WSACleanup();
		exit(1);
	}

	// Recieving filesize
	char fileSizeBuf[sizeof(std::uint64_t)];
	std::uint64_t fileSize = 0;
	std::uint64_t fileSizeMask = 255;
	recv(socketTCP, fileSizeBuf, sizeof(std::uint64_t), NULL);
	for (int i = sizeof(std::uint64_t) - 1; i >= 0; --i)
	{
		fileSize <<= 8;
		fileSizeMask &= fileSizeBuf[i];
		fileSize |= fileSizeMask;
		fileSizeMask = 255;
	}
	
	// Recieving datagramms	
	std::uint32_t countOfEqualFragments = fileSize / datagrammSize;
	std::uint32_t sizeOfModulo = fileSize % datagrammSize;
	std::vector<char*> datagramms;
	char* buf = nullptr;
	std::uint32_t datagrammID = 0;
	bool ACK = true;
	
	if (fileSize >= datagrammSize)
	{
		for (std::uint32_t i = 0; i < countOfEqualFragments; ++i)
		{

			buf = new char[datagrammSize + datagrammIDSize];

			do
			{
				//std::cout << "sending request to " << i << "\n";
				recvfrom(socketUDP, buf, datagrammSize + datagrammIDSize, NULL, (SOCKADDR*)&their_addr, &addr_len);
				datagrammID = getIDfromDatagramm(buf, datagrammSize);
				//if (i == datagrammID) { std::cout << "CORRECT GET" << i << "\n"; }
				//else { std::cout << "WRONG GE????? " << i << "\n"; }

			} while (i != datagrammID);

			datagramms.push_back(buf);
			send(socketTCP, (char*)&ACK, sizeof(ACK), NULL);
		}
		buf = nullptr;

		if (sizeOfModulo)
		{
			++datagrammID;
			buf = new char[sizeOfModulo + datagrammIDSize];
			std::uint32_t datagrammModuloID = 0;
			do
			{
				recvfrom(socketUDP, buf, sizeOfModulo + datagrammIDSize, NULL, (SOCKADDR*)&their_addr, &addr_len);
				datagrammModuloID = getIDfromDatagramm(buf, sizeOfModulo);
			} while (datagrammModuloID != datagrammID);

			datagramms.push_back(buf);
			buf = nullptr;
			send(socketTCP, (char*)&ACK, sizeof(ACK), NULL);
		}
	}
	else
	{
		buf = new char[fileSize + datagrammIDSize];
		do
		{
			recvfrom(socketUDP, buf, fileSize + datagrammIDSize, NULL, (SOCKADDR*)&their_addr, &addr_len);
			datagrammID = getIDfromDatagramm(buf, fileSize);
			
		} while (datagrammID != 0);
		datagramms.push_back(buf);
		buf = nullptr;
		send(socketTCP, (char*)&ACK, sizeof(ACK), NULL);
	}	
	
	bool transmissionFileSuccess = false;
	recv(socketTCP, (char*)&transmissionFileSuccess, sizeof(transmissionFileSuccess), NULL);
	
	// Saving file
	std::string strTransmissionFileName = transmissionFileName;
	std::ofstream transmissionFile(downloadDirectory + "\\" + strTransmissionFileName, std::ios::binary);
	if (fileSize >= datagrammSize)
	{
		if (sizeOfModulo)
		{
			writeToFile(transmissionFile, datagramms, datagrammSize, 0, datagramms.size() - 1);
			writeToFile(transmissionFile, datagramms, sizeOfModulo, datagramms.size() - 1, datagramms.size());
		}
		else
		{
			writeToFile(transmissionFile, datagramms, datagrammSize, 0, datagramms.size());
		}
	}
	else
	{
		writeToFile(transmissionFile, datagramms, fileSize, 0, 1);
	}
	
	
	// Freeing memory 
	transmissionFile.close();
	
	for (auto in : datagramms)
	{
		delete[] in;
		in = nullptr;
	}

	delete[] transmissionFileName, UDPPort;
	transmissionFileName = nullptr; UDPPort = nullptr;
	
	closesocket(socketUDP);
	closesocket(socketTCP);
	freeaddrinfo(addrResultUDP);
	freeaddrinfo(addrResult);
	WSACleanup();

			
	return 0;
}