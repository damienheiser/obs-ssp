/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#include <QMetaType>
#include "ssp-settings.h"
#include "ssp-controller.h"
#include <obs-module.h>

CameraStatus::CameraStatus() : QObject()
{
	controller = new CameraController(this);

	qRegisterMetaType<StatusUpdateCallback>("StatusUpdateCallback");
	qRegisterMetaType<StatusReasonUpdateCallback>("StatusReasonUpdateCallback");
	connect(this, SIGNAL(onSetStream(int, QString, bool, QString, QString, int, StatusReasonUpdateCallback)), this,
		SLOT(doSetStream(int, QString, bool, QString, QString, int, StatusReasonUpdateCallback)));
	connect(this, SIGNAL(onSetLed(bool)), this, SLOT(doSetLed(bool)));
	connect(this, SIGNAL(onRefresh(StatusUpdateCallback)), this, SLOT(doRefresh(StatusUpdateCallback)));
};

void CameraStatus::setIp(const QString &ip)
{
	controller->setIp(ip);
}

void CameraStatus::getResolution(const StatusUpdateCallback &callback)
{
	controller->getCameraConfig(CONFIG_KEY_MOVIE_RESOLUTION, [=](HttpResponse *rsp) {
		if (rsp->statusCode == 999) {
			callback(false);
			return false;
		}
		if (rsp->choices.count() > 0) {
			resolutions.clear();
		}
		for (const auto &i : rsp->choices) {
			resolutions.push_back(i);
		}

		current_resolution = rsp->currentValue;

		callback(true);
		return true;
	});
}

void CameraStatus::getFramerate(const StatusUpdateCallback &callback)
{
	controller->getCameraConfig(CONFIG_KEY_PROJECT_FPS, [=](HttpResponse *rsp) {
		if (rsp->statusCode == 999) {
			callback(false);
			return false;
		}

		if (rsp->choices.count() > 0) {
			framerates.clear();
		}
		for (const auto &i : rsp->choices) {
			framerates.push_back(i);
		}
		current_framerate = rsp->currentValue;
		callback(true);
		return true;
	});
}

void CameraStatus::getCurrentStream(const StatusUpdateCallback &callback)
{
	controller->getCameraConfig(CONFIG_KEY_SEND_STREAM, [=](HttpResponse *rsp) {
		if (rsp->statusCode == 999) {
			callback(false);
			return false;
		}
		current_stream = rsp->currentValue;
		controller->getStreamInfo(rsp->currentValue.toLower(), [=](HttpResponse *rsp) {
			if (rsp->statusCode == 999) {
				callback(false);
				return false;
			}
			current_streamInfo = rsp->streamInfo;
			callback(true);
			return true;
		});
		return true;
	});
}

void CameraStatus::refreshAll(const StatusUpdateCallback &cb)
{
	emit onRefresh(cb);
}

void CameraStatus::doRefresh(StatusUpdateCallback cb)
{
	model.clear();
	getInfo([=](bool ok) {
		if (!ok)
			return cb(false);
		getResolution([=](bool ok) {
			if (!ok)
				return cb(false);
			getFramerate([=](bool ok) {
				if (!ok)
					return cb(false);
				controller->getCameraConfig(CONFIG_KEY_VIDEO_ENCODER, [=](HttpResponse *rsp) {
					if (rsp->statusCode != 200 || rsp->code != 0)
						return cb(false);
					current_encoder = rsp->currentValue;
					encoders.clear();
					for (const auto &choice : rsp->choices)
						if (choice == "H.264" || choice == "H.265")
							encoders.append(choice);
					controller->getCameraConfig(CONFIG_KEY_MOVIE_FORMAT, [=](HttpResponse *rsp) {
						movie_formats = rsp->statusCode == 200 && rsp->code == 0
									? QStringList(rsp->choices)
									: QStringList();
						getCurrentStream(cb);
					});
				});
			});
		});
	});
}
void CameraStatus::getInfo(const StatusUpdateCallback &callback)
{
	controller->getInfo([=](HttpResponse *rsp) {
		if (rsp->statusCode == 999) {
			callback(false);
			return false;
		}
		model = rsp->currentValue;
		callback(true);
		return true;
	});
}
void CameraStatus::setLed(bool isOn)
{
	emit onSetLed(isOn);
}

void CameraStatus::doSetLed(bool isOn)
{
	controller->setCameraConfig(CONFIG_KEY_LED, isOn ? "On" : "Off", [](HttpResponse *) {});
}

void CameraStatus::applyConfig(QString key, QString value, StatusReasonUpdateCallback cb)
{
	controller->getCameraConfig(key, [=](HttpResponse *rsp) {
		if (rsp->statusCode != 200 || rsp->code != 0)
			return cb(false, QString("Could not read %1").arg(key));
		if (rsp->currentValue == value)
			return cb(true, {});
		if (rsp->readOnly || (!rsp->choices.isEmpty() && !rsp->choices.contains(value)))
			return cb(false, QString("Camera does not support %1 = %2").arg(key, value));
		controller->setCameraConfig(key, value, [=](HttpResponse *rsp) {
			if (rsp->statusCode != 200 || rsp->code != 0)
				return cb(false, QString("Could not set %1 = %2").arg(key, value));
			cb(true, {});
		});
	});
}

void CameraStatus::applyStreamCodec(int stream_index, QString encoder, StatusReasonUpdateCallback cb)
{
	if (stream_index == 0)
		return applyConfig(CONFIG_KEY_VIDEO_ENCODER, encoder, cb);
	auto index = QString("stream%1").arg(stream_index);
	controller->getStreamInfo(index, [=](HttpResponse *rsp) {
		if (rsp->statusCode != 200 || rsp->code != 0)
			return cb(false, "Could not read stream codec");
		if (ssp_camera_encoder(rsp->streamInfo.encoderType_) == encoder)
			return cb(true, {});
		controller->setStreamCodec(index, encoder, [=](HttpResponse *rsp) {
			cb(rsp->statusCode == 200 && rsp->code == 0, "Stream codec update");
		});
	});
}

void CameraStatus::setStream(int stream_index, QString resolution, bool low_noise, QString fps, QString encoder,
			     int bitrate, StatusReasonUpdateCallback cb)
{
	emit onSetStream(stream_index, resolution, low_noise, fps, encoder, bitrate, cb);
}

void CameraStatus::doSetStream(int stream_index, QString resolution, bool low_noise, QString fps, QString encoder,
			       int bitrate, StatusReasonUpdateCallback cb)
{
	resolution = ssp_camera_resolution(resolution, low_noise);
	encoder = ssp_camera_encoder(encoder);
	if (stream_index != 0 && stream_index != 1)
		return cb(false, "Invalid stream index");
	auto index = QString("Stream%1").arg(stream_index);
	applyConfig(CONFIG_KEY_MOVIE_RESOLUTION, resolution, [=](bool ok, QString reason) {
		if (!ok)
			return cb(false, reason);
		applyConfig(CONFIG_KEY_PROJECT_FPS, fps, [=](bool ok, QString reason) {
			if (!ok)
				return cb(false, reason);
			applyStreamCodec(stream_index, encoder, [=](bool ok, QString reason) {
				if (!ok)
					return cb(false, reason);
				applyConfig(CONFIG_KEY_SEND_STREAM, index, [=](bool ok, QString reason) {
					if (!ok)
						return cb(false, reason);
					controller->getStreamInfo(index.toLower(), [=](HttpResponse *rsp) {
						if (rsp->statusCode != 200 || rsp->code != 0)
							return cb(false, "Could not read stream settings");
						// Query bitrate is kbit/s; the write API takes bit/s. Preserve GOP and bit depth.
						if (rsp->streamInfo.bitrate_ == bitrate)
							return cb(true, "Camera settings applied");
						controller->setStreamBitrate(
							index.toLower(), QString::number((int64_t)bitrate * 1000),
							[=](HttpResponse *rsp) {
								cb(rsp->statusCode == 200 && rsp->code == 0,
								   "Camera bitrate update");
							});
					});
				});
			});
		});
	});
}

CameraStatus::~CameraStatus()
{
	controller->cancelAllReqs();
	delete controller;
}
