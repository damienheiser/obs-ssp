#undef NDEBUG
#include "ssp-peer.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QTcpSocket>
#include <QThread>
#include <QtEndian>
#include <cassert>
#include <memory>
#include <cstdio>
using namespace ssp_peer;
static void put32(QByteArray &b, uint32_t n)
{
	char p[4];
	qToBigEndian(n, p);
	b.append(p, 4);
}
static void put64(QByteArray &b, uint64_t n)
{
	char p[8];
	qToBigEndian(n, p);
	b.append(p, 8);
}
static QByteArray wire(uint8_t type, QByteArray body)
{
	QByteArray out;
	put32(out, body.size() + 1);
	out.append((char)type);
	out += body;
	return out;
}
static bool wait(const std::function<bool()> &condition)
{
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < 4000) {
		QCoreApplication::processEvents();
		if (condition())
			return true;
		QThread::msleep(5);
	}
	return false;
}
struct Probe {
	QTcpSocket socket;
	QByteArray nonce, key;
	explicit Probe(uint16_t port)
	{
		socket.setReadBufferSize(4096);
		socket.connectToHost("127.0.0.1", port);
		assert(socket.waitForConnected(1000));
	}
	QByteArray read(qint64 n)
	{
		QByteArray b;
		QElapsedTimer t;
		t.start();
		while (b.size() < n && t.elapsed() < 3000) {
			b += socket.read(n - b.size());
			if (b.size() < n)
				socket.waitForReadyRead(50);
		}
		assert(b.size() == n);
		return b;
	}
	QByteArray frame()
	{
		auto h = read(4);
		auto n = qFromBigEndian<uint32_t>(h.constData());
		assert(n && n <= max_packet);
		return read(n);
	}
	void hello()
	{
		auto init = frame();
		assert((uint8_t)init[0] == 0x64 && init.size() == 41 && init.mid(1, 4) == "OSSP");
		nonce = init.mid(9);
	}
	void authenticate(const QByteArray &encoded)
	{
		hello();
		key = QByteArray::fromHex(encoded);
		auto p = QMessageAuthenticationCode::hash(QByteArray("obs-ssp/client/v1\0", 18) + nonce, key,
							  QCryptographicHash::Sha256);
		socket.write(wire(0xc8, p));
		assert(socket.waitForBytesWritten(1000));
		auto ack = frame();
		assert((uint8_t)ack[0] == 0x65);
		assert(ack.mid(1) == QMessageAuthenticationCode::hash(QByteArray("obs-ssp/server/v1\0", 18) + nonce,
								      key, QCryptographicHash::Sha256));
		auto manifest = frame();
		assert((uint8_t)manifest[0] == 0x6e);
		auto media_key = QMessageAuthenticationCode::hash(QByteArray("obs-ssp/media/server/v1") + nonce, key,
								  QCryptographicHash::Sha256);
		assert(manifest.last(32) == QMessageAuthenticationCode::hash(manifest.first(manifest.size() - 32),
									     media_key, QCryptographicHash::Sha256));
	}
	QByteArray heartbeat(uint64_t sequence)
	{
		QByteArray b;
		put64(b, sequence);
		QByteArray signed_data(1, (char)0xcb);
		signed_data += b;
		auto k = QMessageAuthenticationCode::hash(QByteArray("obs-ssp/media/client/v1") + nonce, key,
							  QCryptographicHash::Sha256);
		b += QMessageAuthenticationCode::hash(signed_data, k, QCryptographicHash::Sha256);
		return wire(0xcb, b);
	}
};
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	assert(argc == 2);
	QFile f(QString::fromUtf8(argv[1]));
	assert(f.open(QIODevice::ReadOnly));
	auto captured = f.readAll();
	assert(captured.size() > 37);
	Track t;
	t.video = true;
	t.codec = "hevc";
	t.width = 4096;
	t.height = 2160;
	t.fps_num = 30000;
	t.fps_den = 1001;
	auto key = generate_key();
	assert(valid_key(key) && key.size() == 64 && key != generate_key());
	assert(!valid_key(QByteArray(64, 'x')));
	Server server;
	assert(server.start("127.0.0.1", 0, key, {t}));
	{
		Probe p(server.port());
		p.authenticate(key);
		assert(wait([&] { return server.status().clients == 1; }));
		auto bad = p.heartbeat(0);
		bad[bad.size() - 1] = bad.back() ^ 1;
		p.socket.write(bad);
		p.socket.flush();
		assert(wait([&] { return server.status().clients == 0; }));
		assert(server.status().rejected == 1);
	}
	{
		Probe p(server.port());
		p.authenticate(key);
		assert(wait([&] { return server.status().clients == 1; }));
		auto message = p.heartbeat(0);
		p.socket.write(message + message);
		p.socket.flush();
		assert(wait([&] { return server.status().clients == 0; }));
		assert(server.status().rejected == 2);
	}
	{
		Probe p(server.port());
		p.hello();
		QByteArray oversized;
		put32(oversized, max_packet + 1);
		p.socket.write(oversized);
		p.socket.flush();
		assert(wait([&] { return p.socket.state() == QAbstractSocket::UnconnectedState; }));
		assert(server.status().rejected == 3);
	}
	{
		std::vector<std::unique_ptr<Probe>> pending;
		for (int i = 0; i < 4; ++i) {
			pending.emplace_back(new Probe(server.port()));
			pending.back()->hello();
		}
		Probe excess(server.port());
		assert(wait([&] { return excess.socket.state() == QAbstractSocket::UnconnectedState; }));
		pending.clear();
	}
	{
		Probe slow(server.port());
		slow.authenticate(key);
		slow.socket.setReadBufferSize(1024);
		assert(wait([&] { return server.status().clients == 1; }));
		Packet p;
		p.video = true;
		p.keyframe = true;
		p.data = captured.mid(37);
		for (int i = 0; i < 128 && server.status().clients; ++i) {
			p.pts_us = p.dts_us = i * 33367;
			if (!server.publish(p))
				break;
		}
		assert(wait([&] { return server.status().clients == 0; }));
		assert(server.status().overflow || server.status().rejected >= 5);
	}
	server.stop();
	puts("PASS actual server rejects tampering, replay, oversized frames and excess clients; slow-reader queues stay bounded");
}
