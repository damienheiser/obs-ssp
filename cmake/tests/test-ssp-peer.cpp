#undef NDEBUG
#include "ssp-peer.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QThread>
#include <QtEndian>
#include <atomic>
#include <cassert>
#include <cstdio>
using namespace ssp_peer;
static bool wait(const std::function<bool()> &condition)
{
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < 6000) {
		QCoreApplication::processEvents();
		if (condition())
			return true;
		QThread::msleep(5);
	}
	return false;
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	assert(argc == 2);
	QFile file(QString::fromUtf8(argv[1]));
	assert(file.open(QIODevice::ReadOnly));
	auto captured = file.readAll();
	assert(captured.size() > 37 && (uint8_t)captured[0] == 0x6f);
	assert(qFromBigEndian<uint32_t>(captured.constData() + 17) == 20);
	Track track;
	track.video = true;
	track.codec = "hevc";
	track.width = 4096;
	track.height = 2160;
	track.fps_num = 30000;
	track.fps_den = 1001;
	auto manifest = manifest_bytes({track});
	QVector<Track> decoded;
	assert(parse_manifest(manifest, decoded));
	assert(decoded.size() == 1 && decoded[0].fps_den == 1001);
	assert(!parse_manifest("{}", decoded));
	auto invalid = track;
	invalid.width = UINT32_MAX;
	assert(manifest_bytes({invalid}).isEmpty());
	assert(manifest_bytes({track, track}).isEmpty());
	QByteArray key(32, Qt::Uninitialized);
	for (int i = 0; i < 32; i += 4)
		qToBigEndian(QRandomGenerator::system()->generate(), key.data() + i);
	key = key.toHex();
	Server server;
	assert(!server.start("127.0.0.1", 0, {}, {track}));
	assert(!server.start("127.0.0.1", 0, QByteArray(16, 'x'), {track}));
	assert(server.start("127.0.0.1", 0, key, {track}));
	assert(server.port());
	std::atomic<unsigned> manifests = 0, packets = 0;
	std::atomic<bool> payload_matches = false;
	Client wrong;
	auto wrong_key = key;
	wrong_key[0] = key[0] == 'a' ? 'b' : 'a';
	wrong.start("127.0.0.1", server.port(), wrong_key, {});
	assert(wait([&] { return server.status().rejected == 1; }));
	wrong.stop();
	assert(server.status().clients == 0);
	Client client;
	Callbacks callbacks;
	callbacks.manifest = [&](const QVector<Track> &tracks) {
		assert(tracks.size() == 1 && tracks[0].codec == "hevc");
		++manifests;
	};
	callbacks.packet = [&](const Packet &packet) {
		payload_matches = packet.data == captured.mid(37);
		++packets;
	};
	client.start("127.0.0.1", server.port(), key, std::move(callbacks));
	assert(wait([&] { return manifests == 1; }));
	assert(server.status().clients == 1);
	Packet p;
	p.video = true;
	p.index = 0;
	p.pts_us = 1234567;
	p.dts_us = 1234500;
	p.keyframe = false;
	p.data = captured.mid(37);
	auto bytes = packet_bytes(p);
	Packet back;
	assert(parse_packet(bytes, true, back));
	assert(back.pts_us == p.pts_us && back.dts_us == p.dts_us && back.data == p.data);
	assert(!parse_packet(bytes.left(20), true, back));
	assert(server.publish(p));
	assert(wait([&] { return server.status().packets >= 1; }));
	assert(packets == 0);
	p.keyframe = true;
	assert(server.publish(p));
	assert(wait([&] { return packets == 1; }));
	assert(payload_matches);
	client.stop();
	assert(wait([&] { return server.status().clients == 0; }));
	server.stop();
	assert(!server.port());
	puts("PASS real peer authentication, rejection, captured-byte delivery, late-join gate, manifest bounds and stop");
}
