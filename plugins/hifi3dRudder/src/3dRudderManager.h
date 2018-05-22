//
//  3dRudderManager.h
//  input-plugins/src/input-plugins
//
//  Created by Sam Gondelman on 6/5/15.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi__3dRudderManager_h
#define hifi__3dRudderManager_h

#include <controllers/UserInputMapper.h>
#include <input-plugins/InputPlugin.h>
#include "3drudderSDK.h"
#include "3dRudderInputDevice.h"

class CEvent : public ns3dRudder::IEvent
{
public:
	CEvent(class C3dRudderManager *pPlugin) { _pPlugin = pPlugin; }
	void OnConnect(uint32_t nDeviceNumber);
	void OnDisconnect(uint32_t nDeviceNumber);
	class C3dRudderManager *_pPlugin;
};

class C3dRudderManager : public InputPlugin {
    Q_OBJECT

public:
    // Plugin functions
    bool isSupported() const override;
    const QString getName() const override { return NAME; }
    const QString getID() const override { return _3DRUDDER_ID_STRING; }

    QStringList getSubdeviceNames() override;

    void init() override;
    void deinit() override;

    /// Called when a plugin is being activated for use.  May be called multiple times.
    bool activate() override;
    /// Called when a plugin is no longer being used.  May be called multiple times.
    void deactivate() override;

    void pluginFocusOutEvent() override;
    void pluginUpdate(float deltaTime, const controller::InputCalibrationData& inputCalibrationData) override;

    virtual void saveSettings() const override;
    virtual void loadSettings() override;

signals:
    void RudderAdded(uint32_t nDeviceNumber);
    void RudderRemoved(uint32_t nDeviceNumber);

private:
	ns3dRudder::CSdk *_pSdk;
	bool _isEnabled{ false };
	bool _isInitialized{ false };
	static const char* NAME;
	static const char* _3DRUDDER_ID_STRING;
	QStringList _subdeviceNames;
	CEvent *m_pEvent;
    C3dRudderInputDevice::Pointer _3dRDevice;
    friend CEvent;

};

#endif // hifi__3dRudderManager_h
