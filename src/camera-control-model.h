#pragma once
#include "controller/cameraconfig.h"
#include <QUrl>
#include <QUrlQuery>
#include <vector>

struct SspCameraControl {
	const char *key;
	const char *label;
	const char *group;
};
inline const std::vector<SspCameraControl> &ssp_camera_controls()
{
	static const std::vector<SspCameraControl> controls = {
		{"shoot_mode", "Exposure mode", "Exposure"},
		{"iso", "ISO", "Exposure"},
		{"max_iso", "Maximum ISO", "Exposure"},
		{"sht_operation", "Shutter operation", "Exposure"},
		{"shutter_spd", "Shutter speed", "Exposure"},
		{"shutter_angle", "Shutter angle", "Exposure"},
		{"shutter_time", "Shutter time", "Exposure"},
		{"iris", "Iris", "Exposure"},
		{"ev", "Exposure compensation", "Exposure"},
		{"meter_mode", "Metering", "Exposure"},
		{"flicker", "Flicker control", "Exposure"},
		{"ae_lock", "AE lock", "Exposure"},
		{"wb", "White balance", "White balance"},
		{"mwb", "Color temperature (K)", "White balance"},
		{"tint", "Tint", "White balance"},
		{"wb_priority", "White balance priority", "White balance"},
		{"mwb_r_offset", "Red offset", "White balance"},
		{"mwb_g_offset", "Green offset", "White balance"},
		{"mwb_b_offset", "Blue offset", "White balance"},
		{"mwb_r", "Red gain", "White balance"},
		{"mwb_b", "Blue gain", "White balance"},
		{"focus", "Focus mode", "Focus and lens"},
		{"af_mode", "Autofocus mode", "Focus and lens"},
		{"af_area", "Autofocus area", "Focus and lens"},
		{"caf", "Continuous autofocus", "Focus and lens"},
		{"caf_range", "Autofocus range", "Focus and lens"},
		{"caf_sens", "Autofocus sensitivity", "Focus and lens"},
		{"caf_live", "Live continuous AF", "Focus and lens"},
		{"lens_focus_pos", "Focus position", "Focus and lens"},
		{"lens_zoom_pos", "Zoom position", "Focus and lens"},
		{"lut", "Camera profile / LUT", "Image and color"},
		{"compose_mode", "WDR / composition mode", "Image and color"},
		{"dual_iso", "Dual ISO", "Image and color"},
		{"sharpness", "Sharpness", "Image and color"},
		{"contrast", "Contrast", "Image and color"},
		{"brightness", "Brightness", "Image and color"},
		{"saturation", "Saturation", "Image and color"},
		{"luma_level", "Luma level", "Image and color"},
		{"rotation", "Rotation", "Image and color"},
		{"primary_audio", "Audio encoder", "Audio"},
		{"audio_channel", "Audio channels", "Audio"},
		{"audio_input_gain", "Audio input gain", "Audio"},
		{"audio_output_gain", "Audio output gain", "Audio"},
		{"audio_phantom_power", "Phantom power", "Audio"}};
	return controls;
}
inline QString ssp_camera_control_value(const CameraConfigInfo &control)
{
	return control.type == CONFIG_TYPE_RANGE ? QString::number(control.intValue) : control.currentValue;
}
inline bool ssp_camera_control_valid(const CameraConfigInfo &control, const QString &value)
{
	if (value == ssp_camera_control_value(control))
		return true;
	if (control.readOnly || value.size() > 256)
		return false;
	if (control.type == CONFIG_TYPE_CHOICE)
		return control.choices.contains(value);
	if (control.type == CONFIG_TYPE_RANGE) {
		bool ok = false;
		int number = value.toInt(&ok);
		return ok && number >= control.min && number <= control.max &&
		       (control.step <= 1 || ((int64_t)number - control.min) % control.step == 0);
	}
	return true;
}
inline QString ssp_camera_config_path(const QString &key, const QString &value)
{
	QUrl url("/ctrl/set");
	QUrlQuery query;
	query.addQueryItem(key, value);
	url.setQuery(query);
	return url.toString(QUrl::FullyEncoded);
}
