#define _CRT_SECURE_NO_DEPRECATE

#define DERPNET_STATIC
#include "derpnet.h"

#include <stdio.h>
#include <string.h>

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

static size_t ReadInput(char* Input, size_t InputMaxSize, size_t InputSize)
{
	// super crappy non-blocking windows terminal input reading
	DWORD RecordsRead;
	INPUT_RECORD Records[256];
	ReadConsoleInputA(GetStdHandle(STD_INPUT_HANDLE), Records, ARRAYSIZE(Records), &RecordsRead);

	for (DWORD i = 0; i < RecordsRead; i++)
	{
		if (Records[i].EventType == KEY_EVENT && Records[i].Event.KeyEvent.bKeyDown)
		{
			CHAR Char = Records[i].Event.KeyEvent.uChar.AsciiChar;
			if (Char == '\b' && InputSize)
			{
				printf("\033[1D\033[1X");
				InputSize--;
			}
			else if (Char == '\r')
			{
				Input[InputSize++] = 0;
				printf("\n");
			}
			else if (Char >= 32 && Char < 127)
			{
				Input[InputSize++] = Char;
				printf("%c", Char);
			}
			if (InputSize == InputMaxSize)
			{
				break;
			}
		}
	}

	fflush(stdout);

	return InputSize;
}

// choose any hostname from https://login.tailscale.com/derpmap/default
// both peers do not need to be connected to the same server
// but they must be connected to the same region
#define DERP_SERVER_HOST "derp1f.tailscale.com"

int main()
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

	printf("Connecting to server... ");

	DerpNet Net;
	
	if (!DerpNet_Open(&Net, DERP_SERVER_HOST, &SecretKey))
	{
		printf("ERROR!\n");
		return 1;
	}

	printf("OK!\n");

	printf("Enter PUBLIC key of user to send messages to: ");
	char OtherUserHex[64+1];
	fgets(OtherUserHex, sizeof(OtherUserHex), stdin);
	OtherUserHex[64] = 0;
	DerpKey OtherUser = HexToKey(OtherUserHex);

	// setup terminal input & output
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_PROCESSED_INPUT);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);

	printf("Enter message to send: ");
	fflush(stdout);

	char Input[256];
	size_t InputSize = 0;

	for (;;)
	{
		HANDLE Handles[] = { GetStdHandle(STD_INPUT_HANDLE), Net.SocketEvent };
		DWORD Wait = WSAWaitForMultipleEvents(ARRAYSIZE(Handles), Handles, FALSE, INFINITE, FALSE);

		bool HaveInputFromConsole = Wait == WSA_WAIT_EVENT_0;
		bool HaveDataFromNetwork = Wait == WSA_WAIT_EVENT_0 + 1;

		if (HaveInputFromConsole)
		{
			InputSize = ReadInput(Input, sizeof(Input), InputSize);

			char* NewLine = memchr(Input, 0, InputSize);
			if (NewLine)
			{
				size_t MessageSize = NewLine - Input;
				if (MessageSize == 0)
				{
					break;
				}

				if (!DerpNet_Send(&Net, &OtherUser, Input, InputSize))
				{
					printf("ERROR: disconnected!\n");
					return 1;
				}

				InputSize -= NewLine - Input + 1;

				printf("Enter message to send: ");
				fflush(stdout);
			}
		}

		if (HaveDataFromNetwork)
		{
			for (;;)
			{
				uint8_t* ReceiveData;
				uint32_t ReceiveSize;

				DerpKey ReceiveUser;
				int ReceiveResult = DerpNet_Recv(&Net, &ReceiveUser, &ReceiveData, &ReceiveSize, false);
				if (ReceiveResult == 0)
				{
					break;
				}
				if (ReceiveResult < 0)
				{
					printf("ERROR: disconnected\n");
					return 1;
				}

				// save cursor, scroll down, cursor up, insert line
				printf("\0337\033[1S\033[1A\033[1L");

				printf("Received message from ");
				PrintKey(&ReceiveUser);
				printf(": %.*s", (int)ReceiveSize, (char*)ReceiveData);

				// restore cursor
				printf("\0338");
				fflush(stdout);
			}
		}
	}

	DerpNet_Close(&Net);
}
