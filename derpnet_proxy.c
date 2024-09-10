#define _CRT_SECURE_NO_DEPRECATE

#define DERPNET_STATIC
#include "derpnet.h"

#include <stdio.h>
#include <string.h>

static void PrintHelpAndExit(char* argv0)
{
	printf(
		"USAGE: %s s PORT\n"
		"Serves local port PORT to remote peer\n"
		 "\n"
		"USAGE: %s c other_key LISTEN \n"
		"Connects to remote peer and forwards all incoming traffic to local LISTEN port\n"
		" - other_key = PUBLIC key of user to connect to\n"
		"\n"
		 , argv0, argv0);
	exit(0);
}

static void PrintKey(const DerpKey* Key)
{
	for (size_t i=0; i<32; i++)
	{
		printf("%02hhx", Key->Bytes[i]);
	}
}

static DerpKey HexToKey(const char* Hex)
{
	DERPNET_ASSERT(strlen(Hex) == 64);

	DerpKey Key;
	for (size_t i=0; i<32; i++)
	{
		sscanf(&Hex[2*i], "%02hhx", &Key.Bytes[i]);
	}
	return Key;
}

// choose any hostname from https://login.tailscale.com/derpmap/default
// both peers do not need to be connected to the same server
// but they must be connected to the same region
#define DERP_SERVER_HOST "derp1f.tailscale.com"

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		PrintHelpAndExit(argv[0]);
	}

	if (strcmp(argv[1], "s") == 0)
	{
		if (argc != 3)
		{
			PrintHelpAndExit(argv[0]);
		}

		int ConnectPort = atoi(argv[2]);

		DerpKey MySecretKey;
		DerpNet_CreateNewKey(&MySecretKey);

		DerpKey MyPublicKey;
		DerpNet_GetPublicKey(&MySecretKey, &MyPublicKey);

		printf("My PUBLIC key is: ");
		PrintKey(&MyPublicKey);
		printf("\n");

		DerpNet Net;

		printf("Connecting to DERP server... ");
		if (!DerpNet_Open(&Net, DERP_SERVER_HOST, &MySecretKey))
		{
			printf("ERROR!\n");
			exit(1);
		}
		printf("OK!\n");

		DerpKey RemotePublicKey;
		SOCKET LocalSocket = INVALID_SOCKET;
		printf("Waiting for remote connection...\n");

		for (;;)
		{
			fd_set ReadSet;
			FD_ZERO(&ReadSet);
			FD_SET(Net.Socket, &ReadSet);

			if (LocalSocket != INVALID_SOCKET)
			{
				FD_SET(LocalSocket, &ReadSet);
			}

			int SelectOk = select(0, &ReadSet, NULL, NULL, NULL);
			DERPNET_ASSERT(SelectOk != SOCKET_ERROR);

			// forward all incoming traffic to local socket
			if (FD_ISSET(Net.Socket, &ReadSet))
			{
				if (LocalSocket == INVALID_SOCKET)
				{
					printf("Connecting to '127.0.0.1:%d' ... ", ConnectPort);

					LocalSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
					DERPNET_ASSERT(LocalSocket != INVALID_SOCKET);

					struct sockaddr_in ConnectedAddress =
					{
						.sin_family = AF_INET,
						.sin_addr.s_addr = inet_addr("127.0.0.1"),
						.sin_port = htons(ConnectPort),
					};

					int ConnectOk = connect(LocalSocket, (struct sockaddr*)&ConnectedAddress, sizeof(ConnectedAddress));
					if (ConnectOk != 0)
					{
						printf("ERROR: cannot connect\n");
						closesocket(LocalSocket);
						LocalSocket = INVALID_SOCKET;

						DerpKey OtherPublicKey;
						uint8_t* OtherData;
						uint32_t OtherSize;
						while (DerpNet_Recv(&Net, &OtherPublicKey, &OtherData, &OtherSize, false) > 0)
						{
						}

						printf("Waiting for remote connection...\n");
						continue;
					}

					printf("OK! Connected!\n");
				}

				for (;;)
				{
					DerpKey OtherPublicKey;
					uint8_t* OtherData;
					uint32_t OtherSize;

					int Recv = DerpNet_Recv(&Net, &OtherPublicKey, &OtherData, &OtherSize, false);
					if (Recv < 0)
					{
						printf("ERROR: DERP server disconnected!\n");
						return 1;
					}
					else if (Recv == 0)
					{
						break;
					}

					RemotePublicKey = OtherPublicKey;

					while (OtherSize)
					{
						int Send = send(LocalSocket, (char*)OtherData, OtherSize, 0);
						if (Send < 0)
						{
							printf("Local socket disconnected!\n");
							closesocket(LocalSocket);
							LocalSocket = INVALID_SOCKET;

							while (DerpNet_Recv(&Net, &OtherPublicKey, &OtherData, &OtherSize, false) > 0)
							{
							}

							printf("Waiting for remote connection...\n");
							continue;
						}

						OtherData += Send;
						OtherSize -= Send;
					}
				}
			}

			// forward all outgoing traffic from local socket
			if (LocalSocket != INVALID_SOCKET && FD_ISSET(LocalSocket, &ReadSet))
			{
				char Buffer[8192];
				int Size = recv(LocalSocket, Buffer, sizeof(Buffer), 0);
				if (Size <= 0)
				{
					printf("Local socket closed!\n");
					closesocket(LocalSocket);
					LocalSocket = INVALID_SOCKET;

					DerpNet_Send(&Net, &RemotePublicKey, NULL, 0);
					continue;
				}

				if (!DerpNet_Send(&Net, &RemotePublicKey, Buffer, Size))
				{
					printf("ERROR: DERP server disconnected!\n");
					return 1;
				}
			}
		}
	}
	else if (strcmp(argv[1], "c") == 0)
	{
		if (argc != 4)
		{
			PrintHelpAndExit(argv[0]);
		}

		DerpKey RemoteUserKey = HexToKey(argv[2]);
		int ListenPort = atoi(argv[3]);

		DerpKey MySecretKey;
		DerpNet_CreateNewKey(&MySecretKey);

		DerpKey MyPublicKey;
		DerpNet_GetPublicKey(&MySecretKey, &MyPublicKey);

		DerpNet Net;

		printf("Connecting to DERP server... ");
		if (!DerpNet_Open(&Net, DERP_SERVER_HOST, &MySecretKey))
		{
			printf("ERROR!\n");
			exit(1);
		}
		printf("OK!\n");


		printf("Listening on '127.0.0.1:%d' ... ", ListenPort);

		SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		DERPNET_ASSERT(ListenSocket != INVALID_SOCKET);

		struct sockaddr_in LocalAddress =
		{
			.sin_family = AF_INET,
			.sin_addr.s_addr = inet_addr("127.0.0.1"),
			.sin_port = htons(ListenPort),
		};

		int BindOk = bind(ListenSocket, (struct sockaddr*)&LocalAddress, sizeof(LocalAddress));
		if (BindOk != 0)
		{
			printf("ERROR: cannot bind socket!\n");
			return 1;
		}

		int ListenOk = listen(ListenSocket, 1);
		DERPNET_ASSERT(ListenOk == 0);

		printf("OK!\n");

		printf("Waiting for local connection... ");
		SOCKET LocalSocket = INVALID_SOCKET;

		for (;;)
		{
			fd_set ReadSet;
			FD_ZERO(&ReadSet);
			FD_SET(Net.Socket, &ReadSet);
			if (LocalSocket == INVALID_SOCKET)
			{
				FD_SET(ListenSocket, &ReadSet);
			}
			else
			{
				FD_SET(LocalSocket, &ReadSet);
			}

			int SelectOk = select(0, &ReadSet, NULL, NULL, NULL);
			DERPNET_ASSERT(SelectOk != SOCKET_ERROR);

			// forward all incoming traffic to local socket
			if (FD_ISSET(Net.Socket, &ReadSet))
			{
				bool Disconnect = false;
				for (;;)
				{
					DerpKey OtherPublicKey;
					uint8_t* OtherData;
					uint32_t OtherSize;
					int Recv = DerpNet_Recv(&Net, &OtherPublicKey, &OtherData, &OtherSize, false);
					if (Recv < 0)
					{
						printf("ERROR: DERP server disconnected!\n");
						return 1;
					}
					else if (Recv == 0)
					{
						break;
					}

					if (LocalSocket != INVALID_SOCKET && memcmp(&OtherPublicKey, &RemoteUserKey, sizeof(RemoteUserKey)) == 0)
					{
						if (OtherSize == 0)
						{
							printf("Remote disconnected!\n");
							Disconnect = true;
							break;
						}

						while (OtherSize)
						{
							int Send = send(LocalSocket, (char*)OtherData, OtherSize, 0);
							if (Send <= 0)
							{
								printf("Local socket closed!\n");
								Disconnect = true;
								break;
							}
							OtherData += Send;
							OtherSize -= Send;
						}
					}
				}

				if (Disconnect)
				{
					closesocket(LocalSocket);
					LocalSocket = INVALID_SOCKET;

					DerpKey OtherPublicKey;
					uint8_t* OtherData;
					uint32_t OtherSize;
					while (DerpNet_Recv(&Net, &OtherPublicKey, &OtherData, &OtherSize, false) > 0)
					{
					}

					printf("Waiting for local connection... ");
					continue;
				}
			}

				// forward all outgoing traffic from local socket
			if (LocalSocket != INVALID_SOCKET && FD_ISSET(LocalSocket, &ReadSet))
			{
				char Buffer[8192];
				int Size = recv(LocalSocket, Buffer, sizeof(Buffer), 0);
				if (Size <= 0)
				{
					printf("Local socket closed!\n");
					shutdown(LocalSocket, SD_BOTH);
					closesocket(LocalSocket);
					LocalSocket = INVALID_SOCKET;

					printf("Waiting for local connection... ");
					continue;
				}

				if (!DerpNet_Send(&Net, &RemoteUserKey, Buffer, Size))
				{
					printf("ERROR: DERP server disconnected!\n");
					return 1;
				}
			}

			// accept new connection for local socket
			if (LocalSocket == INVALID_SOCKET && FD_ISSET(ListenSocket, &ReadSet))
			{
				struct sockaddr Address;
				int AddressLen = sizeof(Address);

				LocalSocket = accept(ListenSocket, &Address, &AddressLen);
				DERPNET_ASSERT(LocalSocket != INVALID_SOCKET);

				printf("OK! Connected!\n");
			}
		}
	}
	else
	{
		PrintHelpAndExit(argv[0]);
	}
}
