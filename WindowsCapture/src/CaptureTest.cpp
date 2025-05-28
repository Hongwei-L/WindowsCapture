
#include "CaptureBase/WindowsCaptureBase.h"


#include <Windows.h>
#include <vector>

struct MonitorInfo
{
	MonitorInfo(HMONITOR monitorHandle)
		: hMonitor(monitorHandle)
	{
		MONITORINFO mi = {};
		if (GetMonitorInfo(hMonitor, &mi))
		{
			rcWorkArea = mi.rcMonitor;
			//SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWorkArea, 0);
		}
		else
		{
			rcMonitor = { 0, 0, 0, 0 };
			rcWorkArea = { 0, 0, 0, 0 };
		}
	}
	HMONITOR hMonitor;
	RECT rcMonitor;
	RECT rcWorkArea;
};

std::vector<MonitorInfo> EnumerateAllMonitors()
{
	std::vector<MonitorInfo> monitors;
	EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam)
	{
		auto& monitors = *reinterpret_cast<std::vector<MonitorInfo>*>(lparam);
		monitors.push_back(MonitorInfo(hmon));

		return TRUE;
	}, reinterpret_cast<LPARAM>(&monitors));

	return monitors;
}


int main()
{
	WindowsCaptureBase cap;

	std::vector<MonitorInfo> mon = EnumerateAllMonitors();

	assert(!mon.empty());

	cap.SetCaptureTarget(mon[0].hMonitor);

	while (true)
	{
		cv::Mat ma = cap.GetCaptureImage();
		if (ma.data != NULL)
		{
			cv::imshow("1", ma);
			cv::waitKey(1);
		}
		Sleep(50);
	}


	return 0;
}