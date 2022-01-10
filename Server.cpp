#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <list>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

std::uint32_t datagrammSize = 65500; 
const int datagrammIDSize = 2;

struct RecieveInfo
{
	SOCKET* socketTCP = nullptr;
	SOCKET* socketUDP = nullptr;
	SOCKADDR_STORAGE* their_addr = nullptr;
	socklen_t* addr_len = nullptr;
	std::uint32_t countOfDatagramms = 0;
	std::uint32_t recieveDataSize = 0;
	std::uint32_t datagrammExpecID = 0;
	
};

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

void writeToFile(std::ofstream& file, std::vector<char*>& container, std::uint32_t datagrammDataSize,
	std::uint32_t start, std::uint32_t end)
{
	for (std::uint32_t i = start; i < end; ++i)
	{
		file.write(container[i], datagrammDataSize);
	}
}

std::uint32_t reciveDatagramms(std::vector<char*>& datagramms, RecieveInfo* info)
{
	char* buf = nullptr;
	std::uint32_t datagrammID = 0;
	bool ACK = true;

	for (std::uint32_t i = 0; i < info->countOfDatagramms; ++i)
	{
		buf = new char[info->recieveDataSize + datagrammIDSize];

		do
		{
			recvfrom(*(info->socketUDP), buf, info->recieveDataSize + datagrammIDSize, NULL, (SOCKADDR*)info->their_addr, info->addr_len);
			datagrammID = getIDfromDatagramm(buf, info->recieveDataSize);

		} while (datagrammID != info->datagrammExpecID);
		
		++(info->datagrammExpecID);

		datagramms.push_back(buf);
		send(*(info->socketTCP), (char*)&ACK, sizeof(ACK), NULL);

	}
	buf = nullptr;

	return info->datagrammExpecID;
}


void excludeSocket(std::list<SOCKET*>& openSockets, SOCKET* soc) 
{
	closesocket(*soc);
	openSockets.remove(soc);
}

void exitProgramm(std::list<SOCKET*>& openSockets, std::list<ADDRINFO*> openAddrInfo)
{	
	for (auto& in : openSockets)
	{
		closesocket(*in);
	}
	for (auto& in : openAddrInfo)
	{
		freeaddrinfo(in);
	}
	WSACleanup();
	exit(1);
}

int main(int argc, char* argv[])
{
	const char* IP = argv[1];
	const char* connectionPort = argv[2];
	const std::string downloadDirectory = argv[3];

	std::list<SOCKET*> openSockets;
	std::list<ADDRINFO*> openAddrInfo;
	
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
		exitProgramm(openSockets, openAddrInfo);
	}
	openAddrInfo.push_back(addrResult);

	SOCKET listenSocket = INVALID_SOCKET;
	listenSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
	if (listenSocket == INVALID_SOCKET)
	{
		std::cout << "Socket creation failed";
		exitProgramm(openSockets, openAddrInfo);
	}
	openSockets.push_back(&listenSocket);

	iResult = bind(listenSocket, addrResult->ai_addr, (int)addrResult->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		std::cout << "Binding socket failed\n";
		exitProgramm(openSockets, openAddrInfo);
	}

	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		std::cout << "Listening failed\n";
		exitProgramm(openSockets, openAddrInfo);
	}
	
	SOCKET socketTCP = INVALID_SOCKET;
	socketTCP = accept(listenSocket, NULL, NULL);
	if (socketTCP == INVALID_SOCKET)
	{
		std::cout << "Creating TCP connection failed\n";
		exitProgramm(openSockets, openAddrInfo);
	}
	openSockets.push_back(&socketTCP);

	excludeSocket(openSockets, &listenSocket);

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
		exitProgramm(openSockets, openAddrInfo);
	}
	openAddrInfo.push_back(addrResultUDP);

	SOCKADDR_STORAGE their_addr;
	socklen_t addr_len;
	addr_len = sizeof(their_addr);

	SOCKET socketUDP = INVALID_SOCKET;
	socketUDP = socket(addrResultUDP->ai_family, addrResultUDP->ai_socktype, addrResultUDP->ai_protocol);
	if (socketUDP == INVALID_SOCKET)
	{
		std::cout << "Socket UDP creation failed";
		exitProgramm(openSockets, openAddrInfo);
	}
	openSockets.push_back(&socketUDP);

	iResult = bind(socketUDP, addrResultUDP->ai_addr, (int)addrResultUDP->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		std::cout << "Binding socket failed\n";
		exitProgramm(openSockets, openAddrInfo);
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

	std::uint32_t id = 0;

	RecieveInfo recieveInfo;
	recieveInfo.socketTCP = &socketTCP;
	recieveInfo.socketUDP = &socketUDP;
	recieveInfo.their_addr = &their_addr;
	recieveInfo.addr_len = &addr_len;
		
	if (fileSize >= datagrammSize)
	{
		recieveInfo.countOfDatagramms = countOfEqualFragments;
		recieveInfo.recieveDataSize = datagrammSize;
		recieveInfo.datagrammExpecID = 0;
		id = reciveDatagramms(datagramms, &recieveInfo);
			
		if (sizeOfModulo)
		{
			recieveInfo.countOfDatagramms = 1;
			recieveInfo.recieveDataSize = sizeOfModulo;
			recieveInfo.datagrammExpecID = id;
			reciveDatagramms(datagramms, &recieveInfo);
		}
	}
	else
	{
		recieveInfo.countOfDatagramms = 1;
		recieveInfo.recieveDataSize = fileSize;
		recieveInfo.datagrammExpecID = 0;
		reciveDatagramms(datagramms, &recieveInfo);
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
	
	excludeSocket(openSockets, &socketUDP);
	excludeSocket(openSockets, &socketTCP);
	freeaddrinfo(addrResultUDP);
	freeaddrinfo(addrResult);
	WSACleanup();

			
	return 0;
}