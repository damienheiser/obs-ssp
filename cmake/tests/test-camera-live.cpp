#include "ssp-controller.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (argc < 2)
		return 2;
	CameraStatus camera;
	camera.setIp(QString::fromUtf8(argv[1]));
	QTimer::singleShot(20000, &app, [&]() { app.exit(3); });
	auto verify = [&](bool ok) {
		if (!ok)
			return app.exit(4);
		printf("model=%s resolution=%s fps=%s stream=%s codec=%s bitrate=%d\n",
		       camera.model.toUtf8().constData(), camera.current_resolution.toUtf8().constData(),
		       camera.current_framerate.toUtf8().constData(), camera.current_stream.toUtf8().constData(),
		       camera.current_streamInfo.encoderType_.toUtf8().constData(), camera.current_streamInfo.bitrate_);
		if (argc > 2 &&
		    (camera.current_resolution != "C4K (Low Noise)" || camera.current_framerate != "29.97" ||
		     camera.current_stream != "Stream0" || camera.current_encoder != "H.264"))
			return app.exit(5);
		app.exit(0);
	};
	camera.refreshAll([&](bool ok) {
		if (!ok)
			return app.exit(4);
		if (argc <= 2)
			return verify(true);
		camera.setStream(0, "C4K (Low Noise)", false, "29.97", "H.264", 20000, [&](bool ok, QString reason) {
			printf("apply=%d %s\n", ok, reason.toUtf8().constData());
			if (!ok)
				return app.exit(6);
			camera.refreshAll(verify);
		});
	});
	return app.exec();
}
