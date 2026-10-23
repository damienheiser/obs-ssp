#undef NDEBUG
#include "controller/cameraconfig.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <cassert>
int main()
{
	HttpResponse response = {};
	CameraConfig::parseForStreamInfo(QJsonDocument::fromJson(R"({"code":-1,"msg":"unsupported"})").object(),
					 &response);
	assert(response.code != 0);
	CameraConfig::parseForStreamInfo(
		QJsonDocument::fromJson(
			R"({"streamIndex":"stream0","encoderType":"h264","width":4096,"height":2160,"fps":30,"bitrate":20000,"gop_n":30,"status":"idle"})")
			.object(),
		&response);
	assert(response.code == 0 && response.streamInfo.width_ == 4096 && response.streamInfo.bitrate_ == 20000);
	CameraConfig::parseForConfig(
		QJsonDocument::fromJson(R"({"code":0,"key":"iso","type":1,"ro":true,"value":800,"opts":[800,1600]})")
			.object(),
		&response);
	assert(response.readOnly && response.currentValue == "800" && response.choices.contains("1600"));
	CameraConfig::parseForConfig(
		QJsonDocument::fromJson(
			R"({"code":0,"key":"iso","type":1,"ro":{},"value":"Auto","opts":["Auto","800"]})")
			.object(),
		&response);
	assert(response.readOnly);
	CameraConfig::parseForConfig(
		QJsonDocument::fromJson(R"({"code":0,"key":"iso","type":1,"value":"Auto","opts":["Auto","800"]})")
			.object(),
		&response);
	assert(response.readOnly);
}
