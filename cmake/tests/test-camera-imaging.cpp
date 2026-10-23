#undef NDEBUG
#include "camera-control-model.h"
#include "ssp-source-lifetime.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <chrono>
int main()
{
	CameraConfigInfo c = {};
	c.type = CONFIG_TYPE_CHOICE;
	c.currentValue = "Auto";
	c.choices = {"Auto", "Manual"};
	assert(ssp_camera_control_value(c) == "Auto");
	assert(ssp_camera_control_valid(c, "Manual"));
	assert(!ssp_camera_control_valid(c, "unsupported"));
	c.readOnly = true;
	assert(!ssp_camera_control_valid(c, "Manual"));
	assert(ssp_camera_control_valid(c, "Auto"));
	c = {};
	c.type = CONFIG_TYPE_RANGE;
	c.intValue = 5800;
	c.min = 2300;
	c.max = 30000;
	c.step = 100;
	assert(ssp_camera_control_value(c) == "5800");
	assert(ssp_camera_control_valid(c, "5600"));
	assert(!ssp_camera_control_valid(c, "5650"));
	assert(!ssp_camera_control_valid(c, "99999"));
	auto path = ssp_camera_config_path("lut", "A&B #1");
	assert(path.contains("%26") && path.contains("%23"));
	bool wb = false, iso = false, shutter = false, focus = false, profile = false;
	for (const auto &control : ssp_camera_controls()) {
		QString key = control.key;
		assert(!key.contains("password") && !key.contains("ap_key") && !key.contains("ssid"));
		wb |= key == "wb";
		iso |= key == "iso";
		shutter |= key == "shutter_spd";
		focus |= key == "focus";
		profile |= key == "lut";
	}
	assert(wb && iso && shutter && focus && profile);
	int source = 0;
	auto lifetime = std::make_shared<SspSourceLifetime<int>>();
	lifetime->source = &source;
	auto callback = ssp_with_source(lifetime, [](int *value, int increment) { *value += increment; });
	callback(1);
	assert(source == 1);
	{
		std::lock_guard<std::recursive_mutex> lock(lifetime->mutex);
		lifetime->source = nullptr;
	}
	callback(9);
	assert(source == 1);
	lifetime->source = &source;
	std::atomic<bool> entered = false;
	auto delayed = ssp_with_source(lifetime, [&](int *value) {
		entered = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		++*value;
	});
	std::thread worker([&]() { delayed(); });
	while (!entered.load())
		std::this_thread::yield();
	{
		std::lock_guard<std::recursive_mutex> lock(lifetime->mutex);
		lifetime->source = nullptr;
	}
	worker.join();
	assert(source == 2);
	callback(9);
	assert(source == 2);
}
