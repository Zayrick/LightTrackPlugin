#pragma once

#include <memory>

class QWidget;

namespace lighttrack
{
class AudioService;
class LayoutRepository;
class TimelineBackend;
}

namespace lighttrack::ui
{
QWidget* CreateLightTrackPage(
    std::unique_ptr<TimelineBackend> backend,
    std::unique_ptr<AudioService> audio_service,
    std::unique_ptr<LayoutRepository> layout_repository);
void ReloadLightTrackDevices(QWidget* page);
}
