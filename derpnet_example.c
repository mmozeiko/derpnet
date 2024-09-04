#define _CRT_SECURE_NO_DEPRECATE

#define DERPNET_STATIC
#include "derpnet.h"

#include <stdio.h>
#include <string.h>

static void PrintHelpAndExit(char* argv0)
{
	printf(
		"USAGE: %s s other_key message\n"
		"Sends message to other user:\n"
		" - other_key = PUBLIC key of user to send to\n"
		" - message   = arbitrart text to send\n"
		"\n"
		"USAGE: %s r\n"
		 "Receives messages from other users\n"
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

	if (strcmp(argv[1], "g") == 0)
	{
		DerpKey SecretKey;
		DerpNet_CreateNewKey(&SecretKey);

		DerpKey PublicKey;
		DerpNet_GetPublicKey(&SecretKey, &PublicKey);

		printf("Your PRIVATE key is: ");
		PrintKey(&SecretKey);
		printf(" - do NOT share this with anyone!\n");

		printf("Your PUBLIC key is:  ");
		PrintKey(&PublicKey);
		printf(" - give this to others\n");
	}
	else if (strcmp(argv[1], "s") == 0)
	{
		if (argc != 4)
		{
			PrintHelpAndExit(argv[0]);
		}

		DerpKey MySecretKey;
		DerpNet_CreateNewKey(&MySecretKey);

		DerpKey OtherUser = HexToKey(argv[2]);
		const char* Message = argv[3];

		DerpNet Net;

		printf("Connecting to server... ");
		if (!DerpNet_Open(&Net, DERP_SERVER_HOST, &MySecretKey))
		{
			printf("ERROR!\n");
			exit(1);
		}
		printf("OK!\n");

		printf("Sending message... ");
		if (!DerpNet_Send(&Net, &OtherUser, Message, strlen(Message)))
		{
			printf("ERROR!\n");
			exit(1);
		}
		printf("OK!\n");

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

		printf("Waiting for messages...\n");

		for (;;)
		{
			uint8_t* ReceiveData;
			uint32_t ReceiveSize;
			DerpKey ReceiveUser;

			// true in last argument means to block and wait for incoming data in socket
			int ReceiveResult = DerpNet_Recv(&Net, &ReceiveUser, &ReceiveData, &ReceiveSize, true);
			if (ReceiveResult == 0)
			{
				// this will never happen here
				// but in case you do non-blocking Derp_Recv call, this would mean no more incoming data
				break;
			}
			if (ReceiveResult < 0)
			{
				printf("ERROR!\n");
				exit(1);
			}

			PrintKey(&ReceiveUser);
			printf(": %.*s\n", (int)ReceiveSize, (char*)ReceiveData);
		}

		DerpNet_Close(&Net);
	}
	else
	{
		PrintHelpAndExit(argv[0]);
	}
}
