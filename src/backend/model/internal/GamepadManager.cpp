// Pegasus Frontend
// Copyright (C) 2017-2019  Mátyás Mustoha
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.


#include "GamepadManager.h"

#include "Log.h"
#include "ScriptRunner.h"

#ifdef WITH_SDL_GAMEPAD
#  include "GamepadManagerSDL2.h"
#else
#  include "GamepadManagerQt.h"
#endif

#include <QStringBuilder>


namespace {
void call_gamepad_reconfig_scripts()
{
    ScriptRunner::run(ScriptEvent::CONFIG_CHANGED);
    ScriptRunner::run(ScriptEvent::CONTROLS_CHANGED);
}

inline QString pretty_id(int device_id) {
    return QLatin1String("0x") % QString::number(device_id, 16);
}
} // namespace


namespace model {

GamepadManager::GamepadManager(QObject* parent)
    : QObject(parent)
    , m_log_tag(QStringLiteral("Gamepad"))
    , m_devices(new GamepadListModel(this))
#ifdef WITH_SDL_GAMEPAD
    , m_backend(new GamepadManagerSDL2(this))
#else
    , m_backend(new GamepadManagerQt(this))
#endif
{
    connect(m_backend, &GamepadManagerBackend::connected,
            this, &GamepadManager::bkOnConnected);
    connect(m_backend, &GamepadManagerBackend::disconnected,
            this, &GamepadManager::bkOnDisconnected);
    connect(m_backend, &GamepadManagerBackend::nameChanged,
            this, &GamepadManager::bkOnNameChanged);

    connect(m_backend, &GamepadManagerBackend::buttonConfigured,
            this, &GamepadManager::bkOnButtonCfg);
    connect(m_backend, &GamepadManagerBackend::axisConfigured,
            this, &GamepadManager::bkOnAxisCfg);
    connect(m_backend, &GamepadManagerBackend::configurationCanceled,
            this, &GamepadManager::configurationCanceled);

    connect(m_backend, &GamepadManagerBackend::buttonChanged,
            this, &GamepadManager::bkOnButtonChanged);
    connect(m_backend, &GamepadManagerBackend::axisChanged,
            this, &GamepadManager::bkOnAxisChanged);

    // The Android build also goes through SDL; gamepad buttons are turned into UI navigation here (remote controls still use system key events)
    connect(m_backend, &GamepadManagerBackend::buttonChanged,
            &padbuttonnav, &GamepadButtonNavigation::onButtonChanged);
    connect(m_backend, &GamepadManagerBackend::axisChanged,
            &padaxisnav, &GamepadAxisNavigation::onAxisEvent);

    connect(&padaxisnav, &GamepadAxisNavigation::buttonChanged,
            &padbuttonnav, &GamepadButtonNavigation::onButtonChanged);
}

void GamepadManager::start(const backend::CliArgs& args)
{
    m_backend->start(args);
}

void GamepadManager::stop()
{
    m_backend->stop();
}

void GamepadManager::configureButton(int deviceId, GMButton button)
{
    Q_ASSERT(button != GMButton::Invalid);
    m_backend->start_recording(deviceId, static_cast<GamepadButton>(button));
}
void GamepadManager::configureAxis(int deviceId, GMAxis axis)
{
    Q_ASSERT(axis != GMAxis::Invalid);
    m_backend->start_recording(deviceId, static_cast<GamepadAxis>(axis));
}
void GamepadManager::cancelConfiguration() {
    m_backend->cancel_recording();
}

QString GamepadManager::mappingForAxis(int deviceId, model::GamepadManager::GMAxis axis) const
{
    Q_ASSERT(axis != GMAxis::Invalid);
    return m_backend->mapping_for_axis(deviceId, static_cast<GamepadAxis>(axis));
}

QString GamepadManager::mappingForButton(int deviceId, model::GamepadManager::GMButton button) const
{
    Q_ASSERT(button != GMButton::Invalid);
    return m_backend->mapping_for_button(deviceId, static_cast<GamepadButton>(button));
}

void GamepadManager::bkOnConnected(int device_id, QString name)
{
    if (name.isEmpty())
        name = QLatin1String("generic");

    m_devices->append(new Gamepad(device_id, name, m_devices));
    m_diag_events = 0;

    Log::info(m_log_tag, LOGMSG("Connected device %1 (%2)").arg(pretty_id(device_id), name));
    emit connected(device_id);
}

void GamepadManager::bkOnDisconnected(int device_id)
{
    QString name;

    model::Gamepad* const gamepad = m_devices->findById(device_id);
    if (gamepad) {
        name = gamepad->name();
        m_devices->remove(gamepad);
    }

    Log::info(m_log_tag, LOGMSG("Disconnected device %1 (%2)").arg(pretty_id(device_id), name));
    emit disconnected(std::move(name));
}

void GamepadManager::bkOnNameChanged(int device_id, QString name)
{
    model::Gamepad* const gamepad = m_devices->findById(device_id);
    if (gamepad) {
        Log::info(m_log_tag, LOGMSG("Set name of device %1 to '%2'").arg(pretty_id(device_id), name));
        gamepad->setName(std::move(name));
    }
}

void GamepadManager::bkOnButtonCfg(int device_id, GamepadButton button)
{
    call_gamepad_reconfig_scripts();
    emit buttonConfigured(device_id, static_cast<GMButton>(button));
}

void GamepadManager::bkOnAxisCfg(int device_id, GamepadAxis axis)
{
    call_gamepad_reconfig_scripts();
    emit axisConfigured(device_id, static_cast<GMAxis>(axis));
}

void GamepadManager::bkOnButtonChanged(int device_id, GamepadButton button, bool pressed)
{
    // Diagnostics: the first few button events after each (re)connection, to tell "no events arrive" from "events arrive but nothing reacts".
    if (m_diag_events < 8) {
        m_diag_events++;
        Log::info(m_log_tag, LOGMSG("Button event %1 from device %2: %3 %4")
            .arg(QString::number(m_diag_events), pretty_id(device_id), QString::number(static_cast<int>(button)),
                 pressed ? QStringLiteral("down") : QStringLiteral("up")));
    }
    model::Gamepad* const gamepad = m_devices->findById(device_id);
    if (gamepad)
        gamepad->setButtonState(button, pressed);
}

void GamepadManager::bkOnAxisChanged(int device_id, GamepadAxis axis, double value)
{
    model::Gamepad* const gamepad = m_devices->findById(device_id);
    if (gamepad)
        gamepad->setAxisState(axis, value);
}

} // namespace model

