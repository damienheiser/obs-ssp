#undef NDEBUG
#include "../../ssp_connector/ssp_connector_proto.h"
#include <cassert>
#include <vector>
#include <cstdio>
int main()
{
	Message header = {};
	header.type = VideoDataMsg;
	header.length = SSP_MAX_IPC_BODY_BYTES + 1;
	assert(!ssp_ipc_header_valid(&header));
	header.length = 1;
	assert(!ssp_ipc_header_valid(&header));
	header.type = ConnectorOkMsg;
	header.length = 0;
	assert(ssp_ipc_header_valid(&header));
	header.length = 1;
	assert(!ssp_ipc_header_valid(&header));
	std::vector<uint8_t> storage(sizeof(Message) + sizeof(VideoData) + 4);
	auto message = (Message *)storage.data();
	message->type = VideoDataMsg;
	message->length = sizeof(VideoData) + 4;
	auto video = (VideoData *)message->value;
	video->len = 4;
	assert(ssp_ipc_body_valid(message));
	video->len = 5;
	assert(!ssp_ipc_body_valid(message));
	video->len = SIZE_MAX;
	assert(!ssp_ipc_body_valid(message));
	video->len = 0;
	assert(!ssp_ipc_body_valid(message));
	message->type = AudioDataMsg;
	message->length = sizeof(AudioData) + 4;
	auto audio = (AudioData *)message->value;
	audio->len = 4;
	assert(ssp_ipc_body_valid(message));
	audio->len = SIZE_MAX;
	assert(!ssp_ipc_body_valid(message));
	assert(!ssp_ipc_header_valid(nullptr) && !ssp_ipc_body_valid(nullptr));
	message->type = MetaDataMsg;
	message->length = sizeof(Metadata) - 1;
	assert(!ssp_ipc_header_valid(message));
	message->type = ExceptionMsg;
	message->length = sizeof(Message) + 4;
	auto error = (Message *)message->value;
	error->type = 1;
	error->length = 4;
	error->value[0] = 'e';
	error->value[1] = 'r';
	error->value[2] = 'r';
	error->value[3] = 0;
	assert(ssp_ipc_body_valid(message));
	error->value[3] = 'x';
	assert(!ssp_ipc_body_valid(message));
	error->length = 5;
	assert(!ssp_ipc_body_valid(message));
	puts("PASS IPC pre-allocation bounds and packed metadata/video/exception body validation");
}
