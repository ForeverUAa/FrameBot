#include "macro_inspector.hpp"

MacroEventInspector::MacroEventInspector(MacroTimeline* timeline) : timeline(timeline) {}

void MacroEventInspector::updateFromTimeline() {
    int selectedIdx = timeline->getSelectedEventIndex();

    if (selectedIdx < 0 || selectedIdx >= timeline->getEventCount()) {
        details.hasData = false;
        return;
    }

    const input* evt = timeline->getEvent(selectedIdx);
    if (!evt) {
        details.hasData = false;
        return;
    }

    details.index = selectedIdx;
    details.frame = evt->frame;
    details.subframe = evt->subframe;
    details.button = evt->button;
    details.player2 = evt->player2;
    details.pressed = evt->down;
    details.hasData = true;

    updateNeighboringEvents();
}

void MacroEventInspector::updateNeighboringEvents() {
    if (!details.hasData) return;

    // Find previous event
    details.prevEventIndex = -1;
    details.prevEventFrame = -1;
    for (int i = details.index - 1; i >= 0; i--) {
        const input* evt = timeline->getEvent(i);
        if (evt) {
            details.prevEventIndex = i;
            details.prevEventFrame = evt->frame;
            break;
        }
    }

    // Find next event
    details.nextEventIndex = -1;
    details.nextEventFrame = -1;
    for (int i = details.index + 1; i < timeline->getEventCount(); i++) {
        const input* evt = timeline->getEvent(i);
        if (evt) {
            details.nextEventIndex = i;
            details.nextEventFrame = evt->frame;
            break;
        }
    }
}

void MacroEventInspector::setFrame(int newFrame) {
    if (details.index < 0) return;
    timeline->setEventFrame(details.index, newFrame);
    updateFromTimeline();
}

void MacroEventInspector::setSubframe(double newSubframe) {
    if (details.index < 0) return;
    timeline->setEventSubframe(details.index, newSubframe);
    updateFromTimeline();
}

void MacroEventInspector::setButton(int newButton) {
    if (details.index < 0) return;
    timeline->setEventButton(details.index, newButton);
    updateFromTimeline();
}

void MacroEventInspector::setPlayer(bool player2) {
    if (details.index < 0) return;
    timeline->setEventPlayer(details.index, player2);
    updateFromTimeline();
}

void MacroEventInspector::setPressed(bool pressed) {
    if (details.index < 0) return;
    timeline->setEventAction(details.index, pressed);
    updateFromTimeline();
}
