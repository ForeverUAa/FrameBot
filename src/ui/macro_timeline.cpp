#include "macro_timeline.hpp"

MacroTimeline::MacroTimeline(Macro* macro) : macro(macro) {
    if (macro && !macro->inputs.empty()) {
        sortInputs();
        pushHistoryState();
    }
}

void MacroTimeline::selectEvent(int eventIndex) {
    state.selectedEventIndex = eventIndex;
    if (eventIndex >= 0 && eventIndex < getEventCount()) {
        const auto& evt = macro->inputs[eventIndex];
        setPlayhead(evt.frame, evt.subframe);
    }
}

void MacroTimeline::selectRange(int startIdx, int endIdx) {
    if (startIdx > endIdx) std::swap(startIdx, endIdx);
    state.selectedEventIndex = endIdx;  // Primary selection at end
    // TODO: Implement multi-selection visualization in UI
}

void MacroTimeline::clearSelection() {
    state.selectedEventIndex = -1;
}

bool MacroTimeline::isEventSelected(int eventIndex) const {
    return state.selectedEventIndex == eventIndex;
}

void MacroTimeline::deleteEvent(int eventIndex) {
    if (eventIndex < 0 || eventIndex >= getEventCount()) return;

    pushHistoryState();
    macro->inputs.erase(macro->inputs.begin() + eventIndex);

    if (state.selectedEventIndex == eventIndex) {
        state.selectedEventIndex = -1;
    }
}

void MacroTimeline::deleteSelectedEvents() {
    if (state.selectedEventIndex < 0) return;
    deleteEvent(state.selectedEventIndex);
}

void MacroTimeline::insertEvent(int position, const input& event) {
    if (position < 0) position = 0;
    if (position > getEventCount()) position = getEventCount();

    pushHistoryState();
    macro->inputs.insert(macro->inputs.begin() + position, event);
    sortInputs();
}

void MacroTimeline::moveEvent(int fromIdx, int toIdx) {
    if (fromIdx < 0 || fromIdx >= getEventCount() || toIdx < 0 || toIdx >= getEventCount()) return;
    if (fromIdx == toIdx) return;

    pushHistoryState();
    input evt = macro->inputs[fromIdx];
    macro->inputs.erase(macro->inputs.begin() + fromIdx);
    macro->inputs.insert(macro->inputs.begin() + toIdx, evt);

    state.selectedEventIndex = toIdx;
    sortInputs();
}

void MacroTimeline::duplicateEvent(int eventIndex) {
    if (eventIndex < 0 || eventIndex >= getEventCount()) return;

    const input& evt = macro->inputs[eventIndex];
    insertEvent(eventIndex + 1, evt);
}

void MacroTimeline::setEventFrame(int eventIndex, int newFrame) {
    if (eventIndex < 0 || eventIndex >= getEventCount()) return;

    pushHistoryState();
    macro->inputs[eventIndex].frame = newFrame;
    sortInputs();
}

void MacroTimeline::setEventSubframe(int eventIndex, double newSubframe) {
    if (eventIndex < 0 || eventIndex >= getEventCount()) return;

    // Clamp subframe to valid range
    newSubframe = std::max(0.0, std::min(0.9999, newSubframe));

    pushHistoryState();
    macro->inputs[eventIndex].subframe = newSubframe;
    sortInputs();
}

void MacroTimeline::setEventButton(int eventIndex, int button) {
    if (eventIndex < 0 || eventIndex >= getEventCount()) return;

    pushHistoryState();
    macro->inputs[eventIndex].button = button;
}

void MacroTimeline::setEventPlayer(int eventIndex, bool player2) {
    if (eventIndex < 0 || eventIndex >= getEventCount()) return;

    pushHistoryState();
    macro->inputs[eventIndex].player2 = player2;
}

void MacroTimeline::setEventAction(int eventIndex, bool pressed) {
    if (eventIndex < 0 || eventIndex >= getEventCount()) return;

    pushHistoryState();
    macro->inputs[eventIndex].down = pressed;
}

void MacroTimeline::setPlayhead(int frame, double subframe) {
    state.playheadFrame = frame;
    state.playheadSubframe = std::max(0.0, std::min(0.9999, subframe));
}

void MacroTimeline::setPlayheadPrecise(double preciseFrame) {
    int frame = static_cast<int>(preciseFrame);
    double subframe = preciseFrame - frame;
    setPlayhead(frame, subframe);
}

int MacroTimeline::getNextEventIndex(int fromFrame) const {
    for (int i = 0; i < getEventCount(); i++) {
        if (macro->inputs[i].frame > fromFrame) {
            return i;
        }
    }
    return -1;
}

int MacroTimeline::getPrevEventIndex(int fromFrame) const {
    for (int i = getEventCount() - 1; i >= 0; i--) {
        if (macro->inputs[i].frame < fromFrame) {
            return i;
        }
    }
    return -1;
}

int MacroTimeline::getNearestEventIndex(int frame, double subframe) const {
    int nearest = -1;
    double minDist = std::numeric_limits<double>::max();

    for (int i = 0; i < getEventCount(); i++) {
        double dist = std::abs(macro->inputs[i].getPreciseFrame() - (frame + subframe));
        if (dist < minDist) {
            minDist = dist;
            nearest = i;
        }
    }

    return nearest;
}

const input* MacroTimeline::getEvent(int index) const {
    if (index < 0 || index >= getEventCount()) return nullptr;
    return &macro->inputs[index];
}

void MacroTimeline::sortInputs() {
    std::sort(macro->inputs.begin(), macro->inputs.end());
}

void MacroTimeline::pushHistoryState() {
    // Remove any redo history if we're not at the end
    if (historyIndex >= 0 && historyIndex < (int)history.size() - 1) {
        history.erase(history.begin() + historyIndex + 1, history.end());
    }

    // Add current state to history (limit to 100 states)
    if (history.size() >= 100) {
        history.erase(history.begin());
    }
    history.push_back(macro->inputs);
    historyIndex = history.size() - 1;
}

void MacroTimeline::undo() {
    if (!canUndo()) return;
    historyIndex--;
    macro->inputs = history[historyIndex];
}

void MacroTimeline::redo() {
    if (!canRedo()) return;
    historyIndex++;
    macro->inputs = history[historyIndex];
}

bool MacroTimeline::canUndo() const {
    return historyIndex > 0;
}

bool MacroTimeline::canRedo() const {
    return historyIndex >= 0 && historyIndex < (int)history.size() - 1;
}
