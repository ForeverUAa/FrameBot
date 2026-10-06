#pragma once

#include "../includes.hpp"
#include "../macro.hpp"

/**
 * Core timeline model for the macro editor.
 * Manages timeline operations like selection, navigation, and event manipulation.
 * Decoupled from UI rendering for clean architecture.
 */

class MacroTimeline {
public:
    struct TimelineState {
        int selectedEventIndex = -1;
        int hoveredEventIndex = -1;
        int playheadFrame = 0;
        double playheadSubframe = 0.0;
        bool isDraggingEvent = false;
        int draggedEventIndex = -1;
        bool cbfMode = false;  // CBF/subframe editing mode
        int zoomLevel = 100;   // Percentage (100 = normal, 200 = 2x zoom, etc.)
        int scrollOffsetX = 0; // Horizontal scroll offset in pixels
    };

    struct SelectionRange {
        int startIndex = -1;
        int endIndex = -1;

        bool isEmpty() const { return startIndex < 0 || endIndex < 0; }
        bool contains(int index) const {
            if (isEmpty()) return false;
            return index >= startIndex && index <= endIndex;
        }
    };

    MacroTimeline(Macro* macro);

    // Selection operations
    void selectEvent(int eventIndex);
    void selectRange(int startIdx, int endIdx);
    void clearSelection();
    bool isEventSelected(int eventIndex) const;

    // Event manipulation
    void deleteEvent(int eventIndex);
    void deleteSelectedEvents();
    void insertEvent(int position, const input& event);
    void moveEvent(int fromIdx, int toIdx);
    void duplicateEvent(int eventIndex);

    // Event property editing
    void setEventFrame(int eventIndex, int newFrame);
    void setEventSubframe(int eventIndex, double newSubframe);
    void setEventButton(int eventIndex, int button);
    void setEventPlayer(int eventIndex, bool player2);
    void setEventAction(int eventIndex, bool pressed);

    // Playhead operations
    void setPlayhead(int frame, double subframe = 0.0);
    void setPlayheadPrecise(double preciseFrame);
    int getPlayheadFrame() const { return state.playheadFrame; }
    double getPlayheadSubframe() const { return state.playheadSubframe; }

    // Navigation
    int getNextEventIndex(int fromFrame) const;
    int getPrevEventIndex(int fromFrame) const;
    int getNearestEventIndex(int frame, double subframe = 0.0) const;

    // Timeline queries
    int getEventCount() const { return macro->inputs.size(); }
    const input* getEvent(int index) const;
    const std::vector<input>* getEvents() const { return &macro->inputs; }

    // CBF mode
    void toggleCBFMode() { state.cbfMode = !state.cbfMode; }
    bool isCBFModeEnabled() const { return state.cbfMode; }

    // Zoom and scroll
    void setZoomLevel(int zoom) { state.zoomLevel = std::max(25, std::min(400, zoom)); }
    void zoom(int delta) { setZoomLevel(state.zoomLevel + delta); }
    int getZoomLevel() const { return state.zoomLevel; }

    void setScrollOffset(int offset) { state.scrollOffsetX = offset; }
    void scroll(int delta) { state.scrollOffsetX += delta; }
    int getScrollOffset() const { return state.scrollOffsetX; }

    // State access
    const TimelineState& getState() const { return state; }
    int getSelectedEventIndex() const { return state.selectedEventIndex; }
    int getHoveredEventIndex() const { return state.hoveredEventIndex; }

    // Undo/redo support (basic implementation)
    void pushHistoryState();
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

private:
    Macro* macro;
    TimelineState state;

    // History for undo/redo
    std::vector<std::vector<input>> history;
    int historyIndex = -1;

    void sortInputs();
};
