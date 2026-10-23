#pragma once

#include <QIcon>

namespace DesktopIcons {
enum class Symbol {
    Image, FolderOpen, Save, Export, Batch, Previous, Next, ChevronUp, ChevronDown,
    Fit, ActualSize, Adjustments, Reset, Layers, NewReview, Flag, Check, Close,
    Info, Warning, Application, Search, Clear, Copy, Exit, Crop, Approved, Rejected, Measurements, Calendar, Undo, Redo
};

QIcon icon(Symbol symbol, bool onAccent = false);
}
