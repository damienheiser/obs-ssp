#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>

inline QString ssp_camera_encoder(QString value)
{
	if (value.compare("H264", Qt::CaseInsensitive) == 0)
		return "H.264";
	if (value.compare("H265", Qt::CaseInsensitive) == 0)
		return "H.265";
	return value;
}

inline QString ssp_camera_resolution(QString value, bool low_noise)
{
	if (value == "3840*2160")
		value = "4K";
	else if (value == "4096*2160")
		value = "C4K";
	else if (value == "1920*1080")
		return "1920x1080";
	else
		return value;
	return low_noise ? value + " (Low Noise)" : value;
}

inline QStringList ssp_camera_frame_rates(const QStringList &formats, const QString &resolution,
					  const QStringList &fallback)
{
	QString prefix;
	if (resolution.startsWith("C4K"))
		prefix = "C4KP";
	else if (resolution.startsWith("4K"))
		prefix = "4KP";
	else if (resolution == "1920x1080")
		prefix = "1080P";
	else
		return fallback;
	const bool low_noise = resolution.endsWith(" (Low Noise)");
	QStringList result;
	for (const auto &format : formats) {
		if (!format.startsWith(prefix) || format.endsWith(" (Low Noise)") != low_noise)
			continue;
		QString fps = format.mid(prefix.size()).section(' ', 0, 0);
		bool valid = false;
		fps.toDouble(&valid);
		if (valid && !result.contains(fps))
			result.append(fps);
	}
	return result.isEmpty() ? fallback : result;
}

inline uint64_t ssp_audio_timestamp_ns(bool internal, uint64_t pts_us, uint64_t now_ns, uint64_t offset_frames,
				       uint32_t sample_rate)
{
	return (internal ? now_ns : pts_us * 1000) + (sample_rate ? offset_frames * 1000000000ULL / sample_rate : 0);
}

// Camera stream indices and SSP wire selectors use different numbering.
inline uint32_t ssp_camera_stream_selector(int64_t index)
{
	return index == 0 ? 1 : index == 1 ? 2 : 0;
}

inline QStringList ssp_reported_camera_streams(const QStringList &reported)
{
	QStringList result;
	for (const auto &name : reported)
		if ((name == "Stream0" || name == "Stream1") && !result.contains(name))
			result.append(name);
	return result;
}
