#pragma once

#include <QtCore/QObject>
#include <string>

// Model interface. To be utilized by the presenter.

class IDebugHandler : public QObject
{
public:
	virtual void StartButtonPressed() = 0;
	virtual void StopButtonPressed() = 0;
	virtual std::string GetLogData() = 0;
};