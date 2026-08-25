#pragma once

#include "Commands.h"

class QCommandLineParser;

namespace omapixel {
namespace cli {

Outcome runPluginCommand(const QStringList &words, const QCommandLineParser &parser);

} // namespace cli
} // namespace omapixel
