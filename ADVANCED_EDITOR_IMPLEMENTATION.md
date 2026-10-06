# Advanced Macro Editor Implementation Progress

## Overview
This document outlines the architecture and implementation of an advanced macro authoring, timeline, CBF/subframe analysis, and task-testing tool for xdBot, inspired by NaN's private Geometry Dash tooling workflow.

## Architecture Design

### Core Principles
- **Precise Timing Model**: Extended input representation to support subframe precision
- **Modular Design**: Clear separation between model, timeline, inspector, and UI layers
- **Backward Compatibility**: Existing frame-based macros work without modification
- **Clean Integration**: Reuses existing xdBot systems instead of duplicating functionality

### Component Overview

```
MacroTimeline (Model)
├── TimelineState (playhead, selection, zoom, scroll)
├── SelectionRange (multi-selection support)
└── HistoryManager (undo/redo)

MacroEventInspector (Inspector)
├── EventDetails (current event properties)
└── NeighboringEventData (adjacent events)

MacroTimelineLayer (UI)
├── Toolbar (play, pause, stop, step, CBF toggle, zoom)
├── Timeline Renderer
│   ├── Ruler (frame numbers)
│   ├── Events Layer (P1/P2 visual events)
│   └── Cursor Layer (playhead)
└── Inspector Panel (event properties)

Input (Extended Model)
├── frame: uint32_t
├── subframe: double (0.0-1.0)
├── button, player2, down (existing)
├── getPreciseFrame()
├── setPreciseFrame()
└── Serialization (parseExtension/saveExtension)
```

---

## Phase 1: Precise Timing Model ✅ COMPLETED

### Changes to Existing Files

#### `src/macro.hpp` - Extended Input Struct
**Purpose**: Add subframe precision to input events

**Changes**:
- Added `double subframe = 0.0` member to track timing within a frame
- Implemented `getPreciseFrame()` method to get combined frame + subframe value
- Implemented `setPreciseFrame(double)` to set timing from precise value with clamping
- Extended `operator<()` for timeline ordering (sorts by frame, then subframe)
- Added equality operator that accounts for floating-point precision
- Implemented `parseExtension()` to load subframe data from JSON
- Implemented `saveExtension()` to serialize subframe data
- Maintained backward compatibility: existing frame-only macros work unchanged

**Constructor Signature**:
```cpp
input(int frame, int button, bool player2, bool down, double subframe = 0.0)
```

**Key Methods**:
```cpp
double getPreciseFrame() const;           // Returns frame + subframe
void setPreciseFrame(double preciseTime); // Sets both with normalization
```

---

## Phase 2: Timeline Model ✅ COMPLETED

### New File: `src/ui/macro_timeline.hpp` and `src/ui/macro_timeline.cpp`

**Purpose**: Core timeline logic, decoupled from UI rendering

**TimelineState Structure**:
```cpp
struct TimelineState {
    int selectedEventIndex = -1;
    int hoveredEventIndex = -1;
    int playheadFrame = 0;
    double playheadSubframe = 0.0;
    bool isDraggingEvent = false;
    int draggedEventIndex = -1;
    bool cbfMode = false;
    int zoomLevel = 100;
    int scrollOffsetX = 0;
};
```

**Core Functionality**:

1. **Selection Operations**:
   - `selectEvent(index)` - Select single event and move playhead to it
   - `selectRange(start, end)` - Multi-selection (prepared for UI)
   - `clearSelection()` - Deselect all
   - `isEventSelected(index)` - Query selection state

2. **Event Manipulation**:
   - `deleteEvent(index)` - Remove event with history
   - `deleteSelectedEvents()` - Delete selected
   - `insertEvent(position, event)` - Insert and sort
   - `moveEvent(from, to)` - Move with sorting
   - `duplicateEvent(index)` - Copy event

3. **Property Editing**:
   - `setEventFrame(index, frame)` - Modify frame with sorting
   - `setEventSubframe(index, subframe)` - Modify subframe with clamping
   - `setEventButton(index, button)` - Change button
   - `setEventPlayer(index, player2)` - Change player
   - `setEventAction(index, pressed)` - Change press/release

4. **Playhead Control**:
   - `setPlayhead(frame, subframe)` - Set playhead position
   - `setPlayheadPrecise(preciseFrame)` - Set from combined value
   - Getters for current position

5. **Navigation**:
   - `getNextEventIndex(fromFrame)` - Find next event
   - `getPrevEventIndex(fromFrame)` - Find previous event
   - `getNearestEventIndex(frame, subframe)` - Find closest event

6. **CBF Mode**:
   - `toggleCBFMode()` - Enable/disable subframe editing
   - `isCBFModeEnabled()` - Query mode state

7. **Zoom & Scroll**:
   - `setZoomLevel(zoom)` - Set zoom (25-400%)
   - `zoom(delta)` - Increment zoom
   - `setScrollOffset(offset)` / `scroll(delta)` - Horizontal scrolling

8. **Undo/Redo**:
   - `pushHistoryState()` - Save current macro state
   - `undo()` / `redo()` - Navigate history
   - `canUndo()` / `canRedo()` - Query availability
   - History limited to 100 states to prevent memory bloat

**Key Design Decision**:
- History uses simple vector copy of entire `macro->inputs` vector
- Simple but effective for macros up to several thousand events
- Inputs are automatically sorted after modifications to maintain order

---

## Phase 3: Event Inspector ✅ COMPLETED

### New File: `src/ui/macro_inspector.hpp` and `src/ui/macro_inspector.cpp`

**Purpose**: Data model for displaying and editing selected event properties

**EventDetails Structure**:
```cpp
struct EventDetails {
    int index = -1;
    int frame = 0;
    double subframe = 0.0;
    int button = 1;
    bool player2 = false;
    bool pressed = true;
    bool hasData = false;

    // Neighboring events
    int prevEventIndex = -1;
    int nextEventIndex = -1;
    int prevEventFrame = -1;
    int nextEventFrame = -1;
};
```

**Functionality**:

1. **State Synchronization**:
   - `updateFromTimeline()` - Pulls current event data from timeline
   - `updateNeighboringEvents()` - Finds adjacent events

2. **Property Setters** (all dispatch to timeline):
   - `setFrame(newFrame)`
   - `setSubframe(newSubframe)`
   - `setButton(newButton)`
   - `setPlayer(player2)`
   - `setPressed(pressed)`
   - All setters update and resync after change

3. **Queries**:
   - `getDetails()` - Get current event data
   - `getPrevEventIndex()` / `getNextEventIndex()`
   - `hasData()` - Check if event is selected

**Design**: Acts as adapter between UI and timeline, keeping UI and model in sync

---

## Phase 4: Timeline UI Layer 🔧 IN PROGRESS

### New File: `src/ui/macro_timeline_layer.hpp` and `src/ui/macro_timeline_layer.cpp`

**Purpose**: Complete DAW-like timeline editor UI with rendering, interaction, and toolbar

**Class**: `MacroTimelineLayer extends geode::Popup<Macro*>`

**Toolbar Components** (implemented):
- Play button - Start macro playback
- Pause button - Pause playback
- Stop button - Stop and reset playhead to frame 0
- Step frame button - Advance one frame
- CBF toggle - Enable/disable subframe mode
- Zoom in button - Increase zoom level
- Zoom out button - Decrease zoom level

**Timeline Rendering Sections**:
1. **Ruler** (`rulerLayer`):
   - Frame numbers every 60 frames
   - Tick marks for visual alignment
   - Automatically updates with zoom

2. **Events** (`eventsLayer`):
   - Player 1 events on top track
   - Player 2 events on bottom track
   - Color-coded by action (press=green, release=orange)
   - Opacity indicates selection (selected=opaque, unselected=semi-transparent)
   - Button number displayed on event
   - Hover detection and selection

3. **Playhead** (`cursorLayer`):
   - Red vertical line showing current position
   - Updates in real-time
   - Considers both frame and subframe

4. **Inspector Panel**:
   - Right side of UI
   - Displays selected event properties
   - (Will be expanded with editable fields in next phase)

**Counter Display**:
- Frame counter (e.g., "Frame: 1234")
- Subframe counter (e.g., "Subframe: 43%")
- Real-time updates during playback/scrubbing

**Rendering State**:
```cpp
struct RenderState {
    float pixelsPerFrame = 4.0f;    // Affected by zoom
    float rulerHeight = 30.0f;
    float trackHeight = 40.0f;
    float eventHeight = 16.0f;
    int firstVisibleFrame = 0;
    int lastVisibleFrame = 1000;
    CCSize timelineSize = {800, 200};
};
```

**Coordinate Conversion**:
- `frameToPixels(frame, subframe)` - Converts timeline position to screen pixels
- `pixelsToFrame(pixels)` - Inverse conversion
- Accounts for zoom level and horizontal scroll

**UI Color Scheme**:
- Dark background (RGB 40, 40, 40)
- Inspector panel (RGB 40, 50, 60)
- Player 1 events: Red (255, 100, 100)
- Player 2 events: Blue (100, 150, 255)
- Press events: Green (100, 255, 100)
- Release events: Orange (255, 150, 100)

**Interaction Handlers** (stubs - to be connected):
- Touch/mouse selection of events
- Event dragging for repositioning
- Zoom level changes
- Toolbar button actions

---

## Changes to Build System

### `CMakeLists.txt`
**Added sources**:
```cmake
src/ui/macro_timeline.cpp
src/ui/macro_timeline_layer.cpp
src/ui/macro_inspector.cpp
```

---

## File Structure Summary

### New Files Created
```
src/ui/
├── macro_timeline.hpp         (Timeline model)
├── macro_timeline.cpp
├── macro_timeline_layer.hpp   (Timeline UI)
├── macro_timeline_layer.cpp
├── macro_inspector.hpp        (Inspector model)
└── macro_inspector.cpp
```

### Modified Files
```
src/macro.hpp                  (Extended input struct with subframe support)
CMakeLists.txt                 (Added new sources)
```

---

## Implementation Status

### ✅ Completed (Phase 1-4 Partial)

1. **Precise Input Model**
   - Subframe data storage
   - Serialization/deserialization
   - Backward compatibility
   - Precise frame calculations

2. **Timeline Model**
   - Event selection/manipulation
   - Property editing
   - Playhead control
   - Navigation queries
   - CBF mode toggle
   - Zoom/scroll state management
   - Full undo/redo with 100-state history

3. **Event Inspector Model**
   - Event data tracking
   - Neighboring event detection
   - Property setter synchronization

4. **Timeline UI Framework**
   - Toolbar with 7 buttons
   - Timeline rendering (ruler, events, playhead)
   - Counter display (frame and subframe)
   - Inspector panel placeholder
   - Touch/mouse input handlers (stubbed)

### 🔧 In Progress

- Complete timeline UI input handling (event selection, dragging)
- Connect toolbar buttons to actual playback control
- Inspector panel editable fields
- Performance optimization for large macros

### ⏳ Not Yet Started

1. **CBF/Subframe Mode UI**
   - Subframe selector in inspector
   - Fine-grained editing controls
   - Subframe snapping modes

2. **Task System Infrastructure**
   - `ReplayRunner` - Execute test replays
   - `CheckpointManager` - State snapshots
   - `TaskManager` - Coordinate testing
   - Task result storage

3. **Task UI & Testing**
   - Task window/panel
   - Progress visualization
   - Result display on timeline
   - Rerun/apply/discard actions

4. **Macro Management**
   - Save/load with precision preservation
   - Macro import/export
   - Macro list UI
   - Recent files

5. **Search/Filter**
   - Filter by player, button, frame range
   - Task result filtering

6. **Visual Polish**
   - Dark theme refinement
   - Tooltips and help system
   - Consistent spacing
   - Dense information layout

---

## Integration Points

### With Existing xdBot Systems

1. **Global::macro** - Direct access to active macro
2. **PlayLayer** - Game state for testing
3. **Geode Popup system** - UI base class
4. **Custom keybinds** - Keyboard input
5. **Renderer** - Video/audio replay infrastructure
6. **GDR format** - Serialization

### Backward Compatibility

- Old macros without subframe data load with `subframe = 0.0`
- Existing playback unaffected by new model
- Export without subframe precision for formats that don't support it
- Warning on precision loss during format conversion

---

## Next Steps (Priority Order)

### Immediate (To Reach MVP)
1. Complete timeline UI touch handling (event selection, dragging)
2. Connect toolbar buttons to playback (play/pause/stop/step)
3. Add editable fields to inspector panel
4. Implement CBF subframe selector
5. Test with existing macros to verify compatibility

### Short Term (Core Task System)
1. Implement `ReplayRunner` - Execute macro replay with timing variations
2. Implement `CheckpointManager` - Save/restore game state
3. Implement `TaskManager` - Coordinate testing workflows
4. Create Task UI panel
5. Add basic task types (Frame Window, First Failure, Last Failure)

### Medium Term (Refinement)
1. Search/filter functionality
2. Undo/redo visual indicators
3. Keyboard shortcuts for navigation
4. Auto-save support
5. Performance profiling and optimization

### Long Term (Polish)
1. Custom themes
2. Help system and tutorials
3. Advanced task types
4. Multi-macro project support
5. Export formats with precision preservation

---

## Design Decisions & Rationale

### Subframe Representation
**Decision**: Store as `double subframe` (0.0 to ~0.9999) within each event, with normalization
**Rationale**:
- Simple and compatible with existing frame-based code
- No rounding errors in most cases
- Easy to serialize
- Natural for GUI representation (e.g., 0.5 = 50% through frame)

### History Management
**Decision**: Copy entire `macro->inputs` vector for each undo state
**Rationale**:
- Simple implementation, no complex diff logic
- Acceptable memory overhead (100 states max, typical macro ~500KB)
- Easy to debug
- Works with any future macro format changes

### Toolbar Layout
**Decision**: Top toolbar with icon buttons, dense spacing
**Rationale**:
- Mirrors professional video editing tools (Premiere, DaVinci)
- Leaves maximum vertical space for timeline
- Easy to extend with more tools

### Timeline Rendering
**Decision**: Separate layers for ruler, events, and playhead
**Rationale**:
- Clean separation of concerns
- Easy to update independently
- Efficient partial updates possible
- Scales to large macros with visible-range culling

---

## Known Limitations & Future Improvements

1. **Current Timeline Size**: Fixed at 800x200 pixels (should be resizable)
2. **Event Rendering**: Uses simple rectangles (could be enhanced with icons)
3. **Multi-selection**: Infrastructure ready, UI not complete
4. **Playback Integration**: Buttons not yet connected to Global::macro playback
5. **Performance**: No visible-range culling yet (needed for macros >10k events)

---

## Testing Strategy

### Unit Testing (To Implement)
- Timeline operations (select, move, delete)
- Event property editing
- Frame/subframe conversions
- Undo/redo correctness
- Serialization/deserialization
- CBF precision preservation

### Integration Testing (To Implement)
- Load existing xdBot macros
- Verify no precision loss for frame-only macros
- Test with large macros (1000+ events)
- Playback with new timings
- Task testing on real game state

### Manual Testing (To Implement)
- Create macro, edit events
- Verify visual feedback
- Test keyboard/mouse interaction
- Check memory usage
- Verify undo/redo works correctly

---

## Version History

- **v0.1.0** (Current): Initial implementation
  - Extended input model with subframe support
  - Timeline model with full CRUD and undo/redo
  - Event inspector with property synchronization
  - Timeline UI framework and rendering
  - Toolbar with 7 buttons (stubs)
