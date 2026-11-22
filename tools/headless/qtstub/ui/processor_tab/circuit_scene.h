// Headless stub replacing src/ui/processor_tab/circuit_scene.h (found first via -I order).
#pragma once
#include "kites_qt_stub.h"
namespace Kites
{
class CircuitScene : public QObject
{
  public:
    explicit CircuitScene(QObject *parent = nullptr) : QObject(parent) {}
    void updateCircuitState(const QList<QString> &) {}
};
} // namespace Kites
