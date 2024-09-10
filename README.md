# DerpNet

Simple end-to-end encrypted network library in C for Windows.

Uses Tailscale [DERP][] relays for low bandwidth communications between
peers. This allows exchanging data between peers even when both of them
are behind NAT.

Communications are end-to-end encrypted. Nobody, even relays, cannot read
messages unless they have your PRIVATE key. You only need to share PUBLIC
key to others - which other users will use to send messages to you.

# API

Library is single file header: [derpnet.h][] - it has no other dependencies
than just Windows system libraries.

Before including define either `DERPNET_STATIC` or when using it in
multiple translation units define `DERPNET_IMPLEMENTATION` in one of them.

To generate new PRIVATE key and get PUBLIC key use these functions:

```
void DerpNet_CreateNewKey(DerpKey* UserSecret);
void DerpNet_GetPublicKey(const DerpKey* UserSecret, DerpKey* UserPublic);
```

You can generate as many keys as you want. They are not stored anywhere
or used for any registration - it's up to you to manage these keys.

To connect or disconnect DERP network use:
```
bool DerpNet_Open(DerpNet* Net, const char* DerpServer, const DerpKey* UserSecret);
void DerpNet_Close(DerpNet* Net);
```

You can choose any server from [DERPMAP][] list. Peers that want to communicate
must be connected to the same region - they can be connected to different
servers in the same region.

To send & receive data use:

```
bool DerpNet_Send(DerpNet* Net, const DerpKey* TargetUserPublicKey, const void* Data, size_t DataSize);
int DerpNet_Recv(DerpNet* Net, DerpKey* ReceivedUserPublicKey, uint8_t** ReceivedData, uint32_t* ReceivedSize, bool Wait);
```

There will be no confirmation if destination peer has received sent data.
Send returns false if server disconnected.

Recv call returns 1 when received data from other user - all data is received in
the same packet size as sent. It returns -1 if server disconnected. Or it returns
0 if no data currently has been received. If Wait is set to true, then function
will never return 0 - function always will wait for new data to come in.

# Examples

To compile examples simply run `cl.exe file.c` or `clang-cl.exe file.c`

Example code is not optimized for robustness or performance. Many improvements are
possible to improve error handling and performance. These are just examples.

## derpnet_example

[derpnet_example.c][] - example for API usage to send & receive message.

To receives messages, run:
```
$ derpnet_example.exe r
My PUBLIC key is: a0aaf4763ed56d734672e43b6ffbc9e185da7fda0b40b15c184c4b2385b6d213 - give this to others
Connecting to server... OK!
Waiting for messages...
17998c85c4f1895170c7bfa28bc5a009c9101c652b921cb71ce1e7fcdcb7b24c: Hello World!
```

To send a message, run:
```
$ derpnet_example.exe s a0aaf4763ed56d734672e43b6ffbc9e185da7fda0b40b15c184c4b2385b6d213 "Hello World!"
Connecting to server... OK!
Sending message... OK!
```

## derpnet_file

[derpnet_file.c][] - simple file sharing utility.

To receive file, run:
```
$ derpnet_file.exe r
My PUBLIC key is: 1fe6abe742051e8c78576e999d9423d3ae97495fafaadfcf9592b6e89d97a558
Connecting to server... OK!
Waiting for filename... receiving 'testfile.bin' file
Receiving: 3808 KB - 470.70 KB/s
Done!
Received 4189 KB in 9.0 seconds = 467.55 KB/s
```

To send file - first get public key of receiver, then run:
```
$ derpnet_file.exe s 1fe6abe742051e8c78576e999d9423d3ae97495fafaadfcf9592b6e89d97a558 testfile.bin
Connecting to server... OK!
Sending filename... OK!
Sending: 3936 KB - 482.23 KB/s
Done!
Sent 4189 KB in 8.7 seconds = 479.26 KB/s
```

## derpnet_chat

[derpnet_chat.c][] - simple terminal chat application.

Application sends messages only to single user, but receive messages from multiple ones.

For example, one user runs:
```
$ derpnet_chat.exe
Your PRIVATE key is: 68246df0cc9bf7d752ab059e792e2219d84cbabf5fc2bae047280618467bf355 - do NOT share this with anyone!
Your PUBLIC key is:  4cf72c3bf542d4dc3c6ccbf4b180acb0b07126180f78373da7fb24d9efe06a1f - give this to others
Connecting to server... OK!
Enter PUBLIC key of user to send messages to: ccec67d6ea737063c08b3ae9c33cfad5552863de9d8a7b96551afdc71f03370c
Enter message to send: Hello!
Enter message to send: Testing, 123
Received message from ccec67d6ea737063c08b3ae9c33cfad5552863de9d8a7b96551afdc71f03370c: yes, hello hello!
Enter message to send: 456
Received message from ccec67d6ea737063c08b3ae9c33cfad5552863de9d8a7b96551afdc71f03370c: 123 and 456 received, thank you
Received message from ccec67d6ea737063c08b3ae9c33cfad5552863de9d8a7b96551afdc71f03370c: goodbye
```

And different user runs:
```
$ derpnet_chat.exe
Your PRIVATE key is: d027684d931de511c6b0d1bec6e579df1aa31d48f2c635fac64b28557c57b473 - do NOT share this with anyone!
Your PUBLIC key is:  ccec67d6ea737063c08b3ae9c33cfad5552863de9d8a7b96551afdc71f03370c - give this to others
Connecting to server... OK!
Enter PUBLIC key of user to send messages to: 4cf72c3bf542d4dc3c6ccbf4b180acb0b07126180f78373da7fb24d9efe06a1f
Received message from 4cf72c3bf542d4dc3c6ccbf4b180acb0b07126180f78373da7fb24d9efe06a1f: Hello!
Received message from 4cf72c3bf542d4dc3c6ccbf4b180acb0b07126180f78373da7fb24d9efe06a1f: Testing, 123
Enter message to send: yes, hello hello!
Received message from 4cf72c3bf542d4dc3c6ccbf4b180acb0b07126180f78373da7fb24d9efe06a1f: 456
Enter message to send: 123 and 456 received, thank you
Enter message to send: goodbye
```

# derpnet_proxy

[derpnet_proxy.c][] - TCP port tunneling.

Example to create "proxy" by tunneling TCP port - this is similar to SSH tunneling
but just through DerpNet.

First run `s` command to start server exposing port to DerpNet:
```
$ derpnet_proxy.exe s 8080
My PUBLIC key is: fdb10cf48b7945e3bc9f43c208382939fd9b7b30abd9db6365522271265a3f31
Connecting to DERP server... OK!
Waiting for remote connection...
Connecting to '127.0.0.1:8080' ... OK! Connected!
```

Then run `c` command to connect to previous peer and accept connections on some port:
```
$ derpnet_proxy.exe c fdb10cf48b7945e3bc9f43c208382939fd9b7b30abd9db6365522271265a3f31 8123
Connecting to DERP server... OK!
Listening on '127.0.0.1:8123' ... OK!
Waiting for local connection... OK! Connected!
```
Now anybody connecting to `127.0.0.1:8123` will be actually having all their TCP
traffic redirected to first remote on port `8080`.

# License

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.

[DERP]: https://tailscale.com/kb/1232/derp-servers
[DERPMAP]: https://login.tailscale.com/derpmap/default
[derpnet.h]: derpnet.h
[derpnet_example.c]: derpnet_example.c
[derpnet_file.c]: derpnet_file.c
[derpnet_chat.c]: derpnet_chat.c
[derpnet_proxy.c]: derpnet_proxy.c