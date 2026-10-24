#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <cstdint>
#include <functional>
#include <memory>

namespace ssp_peer {
constexpr uint32_t max_packet = 16 * 1024 * 1024;
constexpr uint32_t max_config = 64 * 1024;
struct Track {
	uint32_t index = 0;
	bool video = false;
	QString codec;
	uint32_t width = 0, height = 0, fps_num = 0, fps_den = 1;
	uint32_t sample_rate = 0, channels = 0;
	QByteArray extra;
};
struct Packet {
	uint32_t index = 0;
	bool video = false, keyframe = false;
	int64_t pts_us = 0, dts_us = 0;
	QByteArray data;
};
struct Callbacks {
	std::function<void(const QVector<Track> &)> manifest;
	std::function<void(const Packet &)> packet;
	std::function<void(const QString &)> state;
};
QByteArray generate_key();
bool valid_key(const QByteArray &key);
QByteArray manifest_bytes(const QVector<Track> &tracks);
bool parse_manifest(const QByteArray &bytes, QVector<Track> &tracks);
QByteArray packet_bytes(const Packet &packet);
bool parse_packet(const QByteArray &bytes, bool video, Packet &packet);
struct Status {
	uint64_t bytes = 0, packets = 0, rejected = 0;
	uint32_t clients = 0;
	bool overflow = false;
};
class Server {
public:
	Server();
	~Server();
	bool start(const QString &bind, uint16_t port, const QByteArray &secret, const QVector<Track> &tracks);
	void stop();
	bool publish(const Packet &packet);
	uint16_t port() const;
	Status status() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
class Client {
public:
	Client();
	~Client();
	void start(const QString &host, uint16_t port, const QByteArray &secret, Callbacks callbacks);
	void stop();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
} // namespace ssp_peer
