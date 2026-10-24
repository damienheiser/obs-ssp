#include "ssp-peer.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <atomic>
#include <deque>
#include <mutex>

namespace ssp_peer {
static uint32_t u32(const char *p)
{
	return qFromBigEndian<uint32_t>(p);
}
static int64_t i64(const char *p)
{
	return (int64_t)qFromBigEndian<uint64_t>(p);
}
static void put32(QByteArray &p, uint32_t n)
{
	char b[4];
	qToBigEndian(n, b);
	p.append(b, 4);
}
static void put64(QByteArray &p, int64_t n)
{
	char b[8];
	qToBigEndian((uint64_t)n, b);
	p.append(b, 8);
}
static QByteArray frame(uint8_t type, const QByteArray &body = {})
{
	if ((uint64_t)body.size() + 1 > max_packet)
		return {};
	QByteArray out;
	put32(out, (uint32_t)body.size() + 1);
	out.append((char)type);
	out += body;
	return out;
}
static bool equal(const QByteArray &a, const QByteArray &b)
{
	if (a.size() != b.size())
		return false;
	unsigned difference = 0;
	for (qsizetype i = 0; i < a.size(); ++i)
		difference |= (uint8_t)a[i] ^ (uint8_t)b[i];
	return difference == 0;
}
static QByteArray proof(const QByteArray &key, const QByteArray &nonce, bool server)
{
	const QByteArray label = server ? QByteArray("obs-ssp/server/v1\0", 18) : QByteArray("obs-ssp/client/v1\0", 18);
	return QMessageAuthenticationCode::hash(label + nonce, key, QCryptographicHash::Sha256);
}
static QByteArray session_key(const QByteArray &secret, const QByteArray &nonce, bool server)
{
	return QMessageAuthenticationCode::hash(
		QByteArray(server ? "obs-ssp/media/server/v1" : "obs-ssp/media/client/v1") + nonce, secret,
		QCryptographicHash::Sha256);
}
static QByteArray protected_frame(uint8_t type, const QByteArray &body, const QByteArray &key, uint64_t &sequence)
{
	if (sequence == UINT64_MAX || (uint64_t)body.size() + 41 > max_packet)
		return {};
	QByteArray payload;
	put64(payload, (int64_t)sequence++);
	payload += body;
	QByteArray signed_data(1, (char)type);
	signed_data += payload;
	payload += QMessageAuthenticationCode::hash(signed_data, key, QCryptographicHash::Sha256);
	return frame(type, payload);
}
static bool unprotect(uint8_t type, QByteArray &body, const QByteArray &key, uint64_t &sequence)
{
	if (body.size() < 40 || sequence == UINT64_MAX || (uint64_t)i64(body.constData()) != sequence)
		return false;
	auto payload = body.first(body.size() - 32);
	QByteArray signed_data(1, (char)type);
	signed_data += payload;
	if (!equal(body.last(32), QMessageAuthenticationCode::hash(signed_data, key, QCryptographicHash::Sha256)))
		return false;
	++sequence;
	body = payload.mid(8);
	return true;
}
static QByteArray nonce()
{
	QByteArray n(32, Qt::Uninitialized);
	for (int i = 0; i < 32; i += 4)
		qToBigEndian(QRandomGenerator::system()->generate(), n.data() + i);
	return n;
}
QByteArray generate_key()
{
	return nonce().toHex();
}
bool valid_key(const QByteArray &key)
{
	if (key.size() != 64)
		return false;
	for (auto c : key)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return false;
	return true;
}
static bool valid(const Track &t)
{
	if (t.extra.size() > max_config)
		return false;
	if (t.video)
		return t.index < 10 && (t.codec == "h264" || t.codec == "hevc" || t.codec == "prores") && t.width &&
		       t.height && t.width <= 8192 && t.height <= 8192 && (uint64_t)t.width * t.height <= 33554432 &&
		       t.fps_num && t.fps_num <= 1000000 && t.fps_den && t.fps_den <= 1000000;
	return t.index < 6 && (t.codec == "aac" || t.codec == "pcm_s16le") && t.sample_rate >= 8000 &&
	       t.sample_rate <= 192000 && t.channels && t.channels <= 8;
}
QByteArray manifest_bytes(const QVector<Track> &tracks)
{
	if (tracks.isEmpty() || tracks.size() > 16)
		return {};
	QJsonArray array;
	QSet<QString> ids;
	for (const auto &t : tracks) {
		QString id = QString(t.video ? "video/%1" : "audio/%1").arg(t.index);
		if (!valid(t) || ids.contains(id))
			return {};
		ids.insert(id);
		QJsonObject o{{"id", id},
			      {"index", (int)t.index},
			      {"kind", t.video ? "video" : "audio"},
			      {"codec", t.codec},
			      {"config", QString::fromLatin1(t.extra.toBase64())}};
		if (t.video) {
			o["width"] = (int)t.width;
			o["height"] = (int)t.height;
			o["fps_num"] = (int)t.fps_num;
			o["fps_den"] = (int)t.fps_den;
		} else {
			o["sample_rate"] = (int)t.sample_rate;
			o["channels"] = (int)t.channels;
		}
		array.append(o);
	}
	return QJsonDocument(QJsonObject{{"application", "obs-ssp"}, {"schema_version", 1}, {"tracks", array}})
		.toJson(QJsonDocument::Compact);
}
bool parse_manifest(const QByteArray &bytes, QVector<Track> &tracks)
{
	if (bytes.size() > 2 * 1024 * 1024)
		return false;
	QJsonParseError error;
	auto doc = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject())
		return false;
	auto root = doc.object();
	if (root["schema_version"].toInt() != 1 || root["application"].toString() != "obs-ssp" ||
	    !root["tracks"].isArray())
		return false;
	QVector<Track> result;
	for (auto value : root["tracks"].toArray()) {
		if (!value.isObject())
			return false;
		auto o = value.toObject();
		Track t;
		auto integer = [&](const char *key) -> uint32_t {
			auto v = o[key];
			double n = v.toDouble(-1);
			return v.isDouble() && n >= 0 && n <= 1000000 && n == (uint32_t)n ? (uint32_t)n : UINT32_MAX;
		};
		t.index = integer("index");
		auto kind = o["kind"].toString();
		if (kind != "video" && kind != "audio")
			return false;
		t.video = kind == "video";
		t.codec = o["codec"].toString();
		if (!o["config"].isString())
			return false;
		auto encoded = o["config"].toString().toLatin1();
		if (encoded.size() > 4 * (int)max_config / 3 + 4)
			return false;
		auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
		if (!decoded)
			return false;
		t.extra = decoded.decoded;
		if (t.video) {
			t.width = integer("width");
			t.height = integer("height");
			t.fps_num = integer("fps_num");
			t.fps_den = integer("fps_den");
		} else {
			t.sample_rate = integer("sample_rate");
			t.channels = integer("channels");
		}
		if (o["id"].toString() != QString(t.video ? "video/%1" : "audio/%1").arg(t.index))
			return false;
		result.push_back(t);
		if (result.size() > 16)
			return false;
	}
	if (manifest_bytes(result).isEmpty())
		return false;
	tracks = std::move(result);
	return true;
}
QByteArray packet_bytes(const Packet &p)
{
	if (p.data.isEmpty() || (uint64_t)p.data.size() + 62 > max_packet || p.index >= (p.video ? 10u : 6u))
		return {};
	QByteArray b;
	put32(b, p.index);
	b.append(p.keyframe ? '\1' : '\0');
	put64(b, p.pts_us);
	put64(b, p.dts_us);
	b += p.data;
	return b;
}
bool parse_packet(const QByteArray &b, bool video, Packet &p)
{
	if (b.size() <= 21 || b.size() > max_packet - 1 || (uint8_t)b[4] > 1)
		return false;
	p.index = u32(b.constData());
	p.video = video;
	p.keyframe = b[4] != 0;
	p.pts_us = i64(b.constData() + 5);
	p.dts_us = i64(b.constData() + 13);
	if (p.index >= (video ? 10u : 6u) || p.pts_us < -10000000LL || p.dts_us < -10000000LL ||
	    p.pts_us > 315360000000000LL || p.dts_us > 315360000000000LL)
		return false;
	p.data = b.mid(21);
	return true;
}
static bool read_frames(QTcpSocket *socket, QByteArray &buffer, const std::function<uint32_t()> &bound,
			const std::function<bool(uint8_t, const QByteArray &)> &handler)
{
	while (socket->bytesAvailable() > 0) {
		auto room = (qsizetype)bound() + 4 - buffer.size();
		if (room <= 0)
			return false;
		buffer += socket->read(room);
		while (buffer.size() >= 4) {
			uint32_t n = u32(buffer.constData());
			if (!n || n > bound())
				return false;
			if (buffer.size() < (qsizetype)n + 4)
				break;
			auto body = buffer.mid(5, n - 1);
			uint8_t type = (uint8_t)buffer[4];
			buffer.remove(0, n + 4);
			if (!handler(type, body))
				return false;
		}
	}
	return true;
}
struct Server::Impl {
	QThread thread;
	QObject *context = nullptr;
	QTcpServer *listener = nullptr;
	QTimer *timer = nullptr;
	struct Session {
		QTcpSocket *socket = nullptr;
		QByteArray input, challenge;
		bool authenticated = false;
		QByteArray tx_key, rx_key;
		uint64_t tx_sequence = 0, rx_sequence = 0;
		QElapsedTimer age, input_age;
		QSet<uint32_t> ready_video;
	};
	QVector<Session *> sessions;
	QByteArray secret, manifest;
	QVector<Track> tracks;
	mutable std::mutex mutex;
	std::deque<Packet> queue;
	size_t queued = 0;
	std::atomic<uint64_t> bytes{0}, packets{0}, rejected{0};
	std::atomic<uint32_t> clients{0};
	std::atomic<bool> overflow{false};
	std::atomic<uint16_t> bound_port{0};
	Impl()
	{
		context = new QObject;
		context->moveToThread(&thread);
		QObject::connect(&thread, &QThread::finished, context, &QObject::deleteLater);
		thread.start();
	}
	~Impl()
	{
		thread.quit();
		thread.wait();
	}
	void remove(Session *s)
	{
		auto i = sessions.indexOf(s);
		if (i < 0)
			return;
		sessions.removeAt(i);
		if (s->authenticated)
			--clients;
		s->socket->disconnect(context);
		s->socket->abort();
		s->socket->deleteLater();
		delete s;
	}
	bool handle(Session *s, uint8_t type, QByteArray body)
	{
		s->input_age.restart();
		if (!s->authenticated) {
			if (type != 0xc8 || body.size() != 32 || !equal(body, proof(secret, s->challenge, false))) {
				return false;
			}
			s->authenticated = true;
			++clients;
			s->socket->setReadBufferSize(max_packet + 4);
			s->tx_key = session_key(secret, s->challenge, true);
			s->rx_key = session_key(secret, s->challenge, false);
			s->socket->write(frame(0x65, proof(secret, s->challenge, true)));
			s->socket->write(protected_frame(0x6e, manifest, s->tx_key, s->tx_sequence));
			return true;
		}
		if (!unprotect(type, body, s->rx_key, s->rx_sequence))
			return false;
		return type == 0xcb && body.isEmpty();
	}
	void accept()
	{
		while (listener->hasPendingConnections()) {
			auto socket = listener->nextPendingConnection();
			if (sessions.size() >= 4) {
				++rejected;
				socket->abort();
				socket->deleteLater();
				continue;
			}
			auto s = new Session;
			s->socket = socket;
			s->challenge = nonce();
			s->age.start();
			s->input_age.start();
			socket->setReadBufferSize(4096);
			sessions.push_back(s);
			QObject::connect(socket, &QTcpSocket::readyRead, context, [this, s] {
				if (!read_frames(
					    s->socket, s->input, [s] { return s->authenticated ? max_packet : 4096u; },
					    [this, s](uint8_t t, const QByteArray &b) { return handle(s, t, b); })) {
					++rejected;
					remove(s);
				}
			});
			QObject::connect(socket, &QTcpSocket::disconnected, context, [this, s] { remove(s); });
			QByteArray hello("OSSP", 4);
			put32(hello, 1);
			hello += s->challenge;
			socket->write(frame(0x64, hello));
		}
	}
	void tick()
	{
		auto copy = sessions;
		for (auto s : copy)
			if ((!s->authenticated && s->age.elapsed() > 5000) || s->input_age.elapsed() > 15000)
				remove(s);
		if (overflow) {
			auto all = sessions;
			for (auto s : all)
				remove(s);
			return;
		}
		std::deque<Packet> pending;
		{
			std::lock_guard<std::mutex> lock(mutex);
			pending.swap(queue);
			queued = 0;
		}
		for (const auto &p : pending) {
			auto body = packet_bytes(p);
			if (body.isEmpty())
				continue;
			auto all = sessions;
			for (auto s : all) {
				if (!s->authenticated)
					continue;
				if (p.video && !s->ready_video.contains(p.index)) {
					if (!p.keyframe)
						continue;
					s->ready_video.insert(p.index);
				}
				if (s->socket->bytesToWrite() + body.size() + 45 > max_packet) {
					++rejected;
					remove(s);
					continue;
				}
				auto data = protected_frame(p.video ? 0x6f : 0x70, body, s->tx_key, s->tx_sequence);
				if (data.isEmpty() || s->socket->write(data) != data.size()) {
					remove(s);
					continue;
				}
				bytes += data.size();
			}
			++packets;
		}
	}
	void stop()
	{
		auto all = sessions;
		for (auto s : all)
			remove(s);
		if (timer) {
			delete timer;
			timer = nullptr;
		}
		if (listener) {
			delete listener;
			listener = nullptr;
		}
		bound_port = 0;
		secret.clear();
		manifest.clear();
		std::lock_guard<std::mutex> lock(mutex);
		tracks.clear();
		queue.clear();
		queued = 0;
	}
};
Server::Server() : impl(new Impl) {}
Server::~Server()
{
	stop();
}
bool Server::start(const QString &bind, uint16_t port, const QByteArray &secret, const QVector<Track> &tracks)
{
	stop();
	auto manifest = manifest_bytes(tracks);
	QHostAddress address;
	if (!valid_key(secret) || manifest.isEmpty() || !address.setAddress(bind))
		return false;
	bool ok = false;
	QMetaObject::invokeMethod(
		impl->context,
		[&] {
			impl->secret = QByteArray::fromHex(secret);
			{
				std::lock_guard<std::mutex> lock(impl->mutex);
				impl->tracks = tracks;
			}
			impl->manifest = manifest;
			impl->overflow = false;
			impl->bytes = 0;
			impl->packets = 0;
			impl->rejected = 0;
			impl->listener = new QTcpServer(impl->context);
			impl->listener->setMaxPendingConnections(4);
			QObject::connect(impl->listener, &QTcpServer::newConnection, impl->context,
					 [this] { impl->accept(); });
			ok = impl->listener->listen(address, port);
			if (!ok) {
				impl->stop();
				return;
			}
			impl->bound_port = impl->listener->serverPort();
			impl->timer = new QTimer(impl->context);
			QObject::connect(impl->timer, &QTimer::timeout, impl->context, [this] { impl->tick(); });
			impl->timer->start(5);
		},
		Qt::BlockingQueuedConnection);
	return ok;
}
void Server::stop()
{
	QMetaObject::invokeMethod(impl->context, [this] { impl->stop(); }, Qt::BlockingQueuedConnection);
}
bool Server::publish(const Packet &p)
{
	if (!impl->bound_port || impl->overflow || p.data.isEmpty() || p.data.size() + 62 > max_packet)
		return false;
	std::lock_guard<std::mutex> lock(impl->mutex);
	bool known = false;
	for (const auto &t : impl->tracks)
		if (t.video == p.video && t.index == p.index)
			known = true;
	if (!known)
		return false;
	if (impl->queue.size() >= 512 || impl->queued + (size_t)p.data.size() > 32 * 1024 * 1024) {
		impl->overflow = true;
		return false;
	}
	impl->queued += p.data.size();
	impl->queue.push_back(p);
	return true;
}
uint16_t Server::port() const
{
	return impl->bound_port;
}
Status Server::status() const
{
	return {impl->bytes, impl->packets, impl->rejected, impl->clients, impl->overflow};
}
struct Client::Impl {
	QThread thread;
	QObject *context = nullptr;
	QTcpSocket *socket = nullptr;
	QTimer *timer = nullptr;
	QByteArray secret, challenge, input, tx_key, rx_key;
	uint64_t tx_sequence = 0, rx_sequence = 0;
	Callbacks callbacks;
	QVector<Track> tracks;
	QElapsedTimer age, last_input, last_heartbeat;
	bool authenticated = false, got_manifest = false;
	Impl()
	{
		context = new QObject;
		context->moveToThread(&thread);
		QObject::connect(&thread, &QThread::finished, context, &QObject::deleteLater);
		thread.start();
	}
	~Impl()
	{
		thread.quit();
		thread.wait();
	}
	void state(const QString &s)
	{
		if (callbacks.state)
			callbacks.state(s);
	}
	void fail(const QString &s)
	{
		if (timer)
			timer->stop();
		state(s);
		if (socket)
			socket->abort();
	}
	bool handle(uint8_t type, QByteArray body)
	{
		last_input.restart();
		if (!authenticated) {
			if (challenge.isEmpty()) {
				if (type != 0x64 || body.size() != 40 || body.left(4) != "OSSP" ||
				    u32(body.constData() + 4) != 1)
					return false;
				challenge = body.mid(8);
				socket->write(frame(0xc8, proof(secret, challenge, false)));
				return true;
			}
			if (type != 0x65 || !equal(body, proof(secret, challenge, true)))
				return false;
			authenticated = true;
			socket->setReadBufferSize(max_packet + 4);
			tx_key = session_key(secret, challenge, false);
			rx_key = session_key(secret, challenge, true);
			state("authenticated");
			return true;
		}
		if (!unprotect(type, body, rx_key, rx_sequence))
			return false;
		if (type == 0x6e) {
			if (got_manifest || !parse_manifest(body, tracks))
				return false;
			got_manifest = true;
			if (callbacks.manifest)
				callbacks.manifest(tracks);
			state("streaming");
			return true;
		}
		if (type == 0x6f || type == 0x70) {
			Packet packet;
			if (!got_manifest || !parse_packet(body, type == 0x6f, packet))
				return false;
			bool known = false;
			for (const auto &t : tracks)
				if (t.video == packet.video && t.index == packet.index)
					known = true;
			if (!known)
				return false;
			if (callbacks.packet)
				callbacks.packet(packet);
			return true;
		}
		return false;
	}
	void stop()
	{
		callbacks = {};
		if (timer) {
			delete timer;
			timer = nullptr;
		}
		if (socket) {
			socket->disconnect(context);
			socket->abort();
			delete socket;
			socket = nullptr;
		}
		secret.clear();
		challenge.clear();
		input.clear();
		tracks.clear();
		tx_key.clear();
		rx_key.clear();
		tx_sequence = rx_sequence = 0;
		authenticated = false;
		got_manifest = false;
	}
};
Client::Client() : impl(new Impl) {}
Client::~Client()
{
	stop();
}
void Client::start(const QString &host, uint16_t port, const QByteArray &secret, Callbacks callbacks)
{
	stop();
	QMetaObject::invokeMethod(
		impl->context,
		[&, callbacks = std::move(callbacks)]() mutable {
			impl->callbacks = std::move(callbacks);
			if (!valid_key(secret) || host.isEmpty() || host.size() > 253 || !port) {
				impl->state("invalid configuration");
				return;
			}
			impl->secret = QByteArray::fromHex(secret);
			impl->socket = new QTcpSocket(impl->context);
			impl->socket->setReadBufferSize(4096);
			impl->age.start();
			impl->last_input.start();
			impl->last_heartbeat.start();
			impl->state("connecting");
			QObject::connect(impl->socket, &QTcpSocket::readyRead, impl->context, [this] {
				if (!read_frames(
					    impl->socket, impl->input,
					    [this] { return impl->authenticated ? max_packet : 4096u; },
					    [this](uint8_t t, const QByteArray &b) { return impl->handle(t, b); }))
					impl->fail("invalid peer response");
			});
			QObject::connect(impl->socket, &QTcpSocket::disconnected, impl->context, [this] {
				if (impl->timer)
					impl->timer->stop();
				impl->state("disconnected");
			});
			QObject::connect(impl->socket, &QTcpSocket::errorOccurred, impl->context,
					 [this](QAbstractSocket::SocketError) {
						 if (impl->timer)
							 impl->timer->stop();
						 impl->state("connection failed");
					 });
			impl->timer = new QTimer(impl->context);
			QObject::connect(impl->timer, &QTimer::timeout, impl->context, [this] {
				if (!impl->authenticated && impl->age.elapsed() > 5000) {
					impl->fail("authentication timeout");
					return;
				}
				if (impl->last_input.elapsed() > 15000) {
					impl->fail("media timeout");
					return;
				}
				if (impl->authenticated && impl->last_heartbeat.elapsed() > 3000) {
					impl->socket->write(protected_frame(0xcb, {}, impl->tx_key, impl->tx_sequence));
					impl->last_heartbeat.restart();
				}
			});
			impl->timer->start(100);
			impl->socket->connectToHost(host, port);
		},
		Qt::BlockingQueuedConnection);
}
void Client::stop()
{
	QMetaObject::invokeMethod(impl->context, [this] { impl->stop(); }, Qt::BlockingQueuedConnection);
}
} // namespace ssp_peer
