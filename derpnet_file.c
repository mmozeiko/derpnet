#define _CRT_SECURE_NO_DEPRECATE

#define DERPNET_STATIC
#include "derpnet.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

static void PrintHelpAndExit(char* argv0)
{
	printf(
		"USAGE: %s s other_user filename\n"
		"Sends file to other user:\n"
		" - other_key = PUBLIC key of user to send to\n"
		" - filename  = file to send\n"
		"\n"
		"USAGE: %s r\n"
		 "Receives one file from first sender\n"
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
		if (argc != 4)
		{
			PrintHelpAndExit(argv[0]);
		}

		DerpKey MySecretKey;
		DerpNet_CreateNewKey(&MySecretKey);

		DerpKey OtherUser = HexToKey(argv[2]);
		const char* FileName = argv[3];

		FILE* File = fopen(FileName, "rb");
		if (!File)
		{
			printf("ERROR: cannot open '%s' file for reading!\n", FileName);
		}

		DerpNet Net;

		printf("Connecting to server... ");
		if (!DerpNet_Open(&Net, DERP_SERVER_HOST, &MySecretKey))
		{
			printf("ERROR!\n");
			exit(1);
		}
		printf("OK!\n");

		printf("Sending filename... ");
		if (!DerpNet_Send(&Net, &OtherUser, FileName, strlen(FileName)))
		{
			printf("ERROR!\n");
			exit(1);
		}
		printf("OK!\n");

		clock_t ClockStart = clock();
		clock_t ClockNext = ClockStart + CLOCKS_PER_SEC;

		size_t TotalSize = 0;

		for (;;)
		{
			clock_t ClockNow = clock();
			if (ClockNow >= ClockNext)
			{
				double Speed = TotalSize / ((double)(ClockNow - ClockStart) / CLOCKS_PER_SEC);
				printf("\rSending: %zu KB - %.2f KB/s", TotalSize / 1024, Speed / 1024);
				ClockNext = ClockNow + CLOCKS_PER_SEC;
			}

			char Buffer[4096];
			size_t BufferSize = fread(Buffer, 1, sizeof(Buffer), File);
			if (BufferSize == 0)
			{
				break;
			}

			if (!DerpNet_Send(&Net, &OtherUser, Buffer, BufferSize))
			{
				printf("ERROR!\n");
				exit(1);
			}

			TotalSize += BufferSize;
		}

		// send 0 sized message for end of file
		if (!DerpNet_Send(&Net, &OtherUser, NULL, 0))
		{
			printf("ERROR!\n");
			exit(1);
		}

		printf("\nDone!\n");
		double Time = (double)(clock() - ClockStart) / CLOCKS_PER_SEC;
		double Speed = TotalSize / Time;
		printf("\rSent %zu KB in %.1f seconds = %.2f KB/s\n", TotalSize / 1024, Time, Speed / 1024);

		fclose(File);
		DerpNet_Close(&Net);
	}
	else if (strcmp(argv[1], "r") == 0)
	{
		if (argc != 2)
		{
			PrintHelpAndExit(argv[0]);
		}

		DerpKey MySecretKey;
		DerpNet_CreateNewKey(&MySecretKey);

		DerpKey MyPublicKey;
		DerpNet_GetPublicKey(&MySecretKey, &MyPublicKey);

		printf("My PUBLIC key is: ");
		PrintKey(&MyPublicKey);
		printf("\n");

		DerpNet Net;

		printf("Connecting to server... ");
		if (!DerpNet_Open(&Net, DERP_SERVER_HOST, &MySecretKey))
		{
			printf("ERROR!\n");
			exit(1);
		}
		printf("OK!\n");

		printf("Waiting for filename... ");

		DerpKey ExpectedUser;
		FILE* File;

		{
			uint8_t* ReceiveData;
			uint32_t ReceiveSize;
			DerpKey ReceiveUser;

			int ReceiveResult = DerpNet_Recv(&Net, &ReceiveUser, &ReceiveData, &ReceiveSize, true);
			if (ReceiveResult <= 0)
			{
				printf("ERROR!\n");
				exit(1);
			}

			char FileName[256];
			snprintf(FileName, sizeof(FileName), "%.*s", (int)ReceiveSize, (char*)ReceiveData);

			if (strchr(FileName, '/') || strchr(FileName, '\\'))
			{
				printf("ERROR: filename contains bad characters!");
				exit(1);
			}

			printf("receiving '%s' file\n", FileName);

			File = fopen(FileName, "wb");
			if (!File)
			{
				printf("ERROR: cannot open '%s' file for writing!\n", FileName);
				exit(1);
			}

			ExpectedUser = ReceiveUser;
		}

		clock_t ClockStart = clock();
		clock_t ClockNext = ClockStart + CLOCKS_PER_SEC;

		size_t TotalSize = 0;

		for (;;)
		{
			uint8_t* ReceiveData;
			uint32_t ReceiveSize;
			DerpKey ReceiveUser;

			int ReceiveResult = DerpNet_Recv(&Net, &ReceiveUser, &ReceiveData, &ReceiveSize, true);
			if (ReceiveResult <= 0)
			{
				printf("ERROR!\n");
				exit(1);
			}

			if (memcmp(&ReceiveUser, &ExpectedUser, sizeof(ReceiveUser)) == 0)
			{
				if (fwrite(ReceiveData, 1, ReceiveSize, File) != ReceiveSize)
				{
					printf("ERROR writing to file\n");
					exit(1);
				}

				// received 0 sized message, end of file
				if (ReceiveSize == 0)
				{
					break;
				}
				TotalSize += ReceiveSize;

				clock_t ClockNow = clock();
				if (ClockNow >= ClockNext)
				{
					double Speed = TotalSize / ((double)(ClockNow - ClockStart) / CLOCKS_PER_SEC);
					printf("\rReceiving: %zu KB - %.2f KB/s", TotalSize / 1024, Speed / 1024);
					ClockNext = ClockNow + CLOCKS_PER_SEC;
				}
			}
		}

		printf("\nDone!\n");
		double Time = (double)(clock() - ClockStart) / CLOCKS_PER_SEC;
		double Speed = TotalSize / Time;
		printf("\rReceived %zu KB in %.1f seconds = %.2f KB/s\n", TotalSize / 1024, Time, Speed / 1024);

		fclose(File);
		DerpNet_Close(&Net);

	}
	else
	{
		PrintHelpAndExit(argv[0]);
	}
}
