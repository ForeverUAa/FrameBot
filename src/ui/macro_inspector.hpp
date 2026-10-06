#pragma once

#include "../includes.hpp"
#include "macro_timeline.hpp"

/**
 * Inspector panel for viewing and editing a selected input event.
 * Shows detailed properties and allows modification of frame, subframe, button, player, etc.
 */

class MacroEventInspector {
public:
    struct EventDetails {
        int index = -1;
        int frame = 0;
        double subframe = 0.0;
        int button = 1;
        bool player2 = false;
        bool pressed = true;  // down
        bool hasData = false;

        // Neighboring events
        int prevEventIndex = -1;
        int nextEventIndex = -1;
        int prevEventFrame = -1;
        int nextEventFrame = -1;
    };

    MacroEventInspector(MacroTimeline* timeline);

    // Update inspection data from timeline
    void updateFromTimeline();

    // Get current details
    const EventDetails& getDetails() const { return details; }

    // Property setters (dispatch to timeline)
    void setFrame(int newFrame);
    void setSubframe(double newSubframe);
    void setButton(int newButton);
    void setPlayer(bool player2);
    void setPressed(bool pressed);

    // Query neighboring events
    int getPrevEventIndex() const { return details.prevEventIndex; }
    int getNextEventIndex() const { return details.nextEventIndex; }

    bool hasData() const { return details.hasData; }

private:
    MacroTimeline* timeline;
    EventDetails details;

    void updateNeighboringEvents();
};
