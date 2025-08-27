#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../util/log.h"
#include "../config.h"

#include "ipc.h"

#define R_THROW(x) do { Result r = x; if (R_FAILED(r)) { fatalThrow(r); }  } while(0)

#define CMD_UPDATE_CONF 1

static Handle handles[2];
static SmServiceName serverName;

static Handle* const serverHandle = &handles[0];
static Handle* const clientHandle = &handles[1];

static bool isClientConnected = false;

static void StartServer()
{
	memcpy(serverName.name, "padmacro", sizeof("padmacro"));
	R_THROW(smRegisterService(serverHandle, serverName, false, 1));
}

static void StopServer()
{
	svcCloseHandle(*serverHandle);
	R_THROW(smUnregisterService(serverName));
}

typedef struct {
	u32 type;
	u64 cmdId;
	void* data;
	u32 dataSize;
} Request;

static Request ParseRequestFromTLS()
{
	Request req = {0};

	void* base = armGetTls();
	HipcParsedRequest hipc = hipcParseRequest(base);

	req.type = hipc.meta.type;

	if (hipc.meta.type == CmifCommandType_Request)
	{
		CmifInHeader* header = (CmifInHeader*)cmifGetAlignedDataStart(hipc.data.data_words, base);
		size_t dataSize = hipc.meta.num_data_words * 4;

		if (!header)
			fatalThrow(1);
		if (dataSize < sizeof(CmifInHeader))
			fatalThrow(2);
		if (header->magic != CMIF_IN_HEADER_MAGIC)
			fatalThrow(3);

		req.cmdId = header->command_id;
		req.dataSize = dataSize - sizeof(CmifInHeader);
		req.data = req.dataSize ? ((u8*)header) + sizeof(CmifInHeader) : NULL;
	}

	return req;
}
static bool ReadPayload(const Request* req, void* data, u32 len)
{
	if (req->dataSize < len || !req->data)
		return false;

	memcpy(data, req->data, len);
	return true;
}
static void WriteResponseToTLS(Result rc)
{
	HipcMetadata meta = { 0 };
	meta.type = CmifCommandType_Request;
	meta.num_data_words = (sizeof(CmifOutHeader) + 0x10) / 4;

	void* base = armGetTls();
	HipcRequest hipc = hipcMakeRequest(base, meta);
	CmifOutHeader* rawHeader = (CmifOutHeader*)cmifGetAlignedDataStart(hipc.data_words, base);

	rawHeader->magic = CMIF_OUT_HEADER_MAGIC;
	rawHeader->result = rc;
	rawHeader->token = 0;
}

static bool HandleCommand(const Request* req)
{
	switch (req->cmdId)
	{
		case CMD_UPDATE_CONF:
			loadConfig();
			log_info("Configuration updated");
			WriteResponseToTLS(0);
			return false;
		default:
			WriteResponseToTLS(1);
			return true;
	}
}
static void WaitAndProcessRequest()
{
	s32 index = -1;

	R_THROW(svcWaitSynchronization(&index, handles, isClientConnected ? 2 : 1, UINT64_MAX));
	if (index == 0)
	{
		Handle newcli;
		// Accept session
		R_THROW(svcAcceptSession(&newcli, *serverHandle));
		
		// Max clients reached
		if (isClientConnected)
			R_THROW(svcCloseHandle(newcli));

		isClientConnected = true;
		*clientHandle = newcli;

	}
	else if (index == 1)
	{
		// Handle message
		if (!isClientConnected) {
			fatalThrow(4);
		}

		s32 _idx;
		R_THROW(svcReplyAndReceive(&_idx, clientHandle, 1, 0, UINT64_MAX));

		bool shouldClose = false;
		Request r = ParseRequestFromTLS();
		switch (r.type)
		{
		case CmifCommandType_Request:
			shouldClose = HandleCommand(&r);
			break;
		case CmifCommandType_Close:
			WriteResponseToTLS(0);
			shouldClose = true;
			break;
		default:
			WriteResponseToTLS(1);
			break;
		}

		Result rc = svcReplyAndReceive(&_idx, clientHandle, 0, *clientHandle, 0);

		if (rc != KERNELRESULT(TimedOut))
			R_THROW(rc);

		if (shouldClose) {
			R_THROW(svcCloseHandle(*clientHandle));
			isClientConnected = false;
			// exec command
		}
	}
	else return;
}

void IpcThread()
{
	StartServer();

	while (1)
		WaitAndProcessRequest();

	StopServer();
}
