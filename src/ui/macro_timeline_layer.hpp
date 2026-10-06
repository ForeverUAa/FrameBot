#pragma once

#include "../includes.hpp"
#include "macro_timeline.hpp"
#include "macro_inspector.hpp"

/**
 * MacroTimelineLayer - Advanced macro timeline editor UI.
 * Renders a DAW-like timeline interface with:
 * - Horizontal frame/subframe axis
 * - Visual input events (press/release)
 * - Player 1/2 separation
 * - Event selection and dragging
 * - Timeline navigation (play, pause, step, jump)
 * - Real-time playback cursor
 * - Zoom and scroll controls
 */

class MacroTimelineLayer : public geode::Popup<Macro*> {
public:
    static MacroTimelineLayer* create(Macro* macro);

    // Touch event handlers (public for modifier access)
    bool onTouchBegan(CCTouch* touch, CCEvent* event);
    void onTouchMoved(CCTouch* touch, CCEvent* event);
    void onTouchEnded(CCTouch* touch, CCEvent* event);

private:
    bool setup() override;
    void keyBackClicked() override;

    Macro* macro = nullptr;
    std::unique_ptr<MacroTimeline> timeline;
    std::unique_ptr<MacroEventInspector> inspector;

    // UI Components
    CCMenu* timelineMenu = nullptr;
    CCMenu* toolbarMenu = nullptr;
    CCLayer* timelineLayer = nullptr;
    CCLayer* rulerLayer = nullptr;     // Timeline ruler/frame counter
    CCLayer* eventsLayer = nullptr;    // Event rendering
    CCLayer* cursorLayer = nullptr;    // Playhead cursor

    // UI Elements
    CCLabelBMFont* frameCounterLabel = nullptr;
    CCLabelBMFont* subframeLabel = nullptr;
    CCLabelBMFont* timeLabel = nullptr;
    CCScale9Sprite* selectedEventBg = nullptr;
    CCScale9Sprite* inspectorBg = nullptr;

    // Toolbar buttons
    CCMenuItemSpriteExtra* playBtn = nullptr;
    CCMenuItemSpriteExtra* pauseBtn = nullptr;
    CCMenuItemSpriteExtra* stopBtn = nullptr;
    CCMenuItemSpriteExtra* stepFrameBtn = nullptr;
    CCMenuItemSpriteExtra* cbfModeToggle = nullptr;
    CCMenuItemSpriteExtra* zoomInBtn = nullptr;
    CCMenuItemSpriteExtra* zoomOutBtn = nullptr;

    // Timeline rendering state
    struct RenderState {
        float pixelsPerFrame = 4.0f;     // Pixels per frame (affected by zoom)
        float rulerHeight = 30.0f;
        float trackHeight = 40.0f;       // Height per player track
        float eventHeight = 16.0f;
        int firstVisibleFrame = 0;
        int lastVisibleFrame = 1000;
        CCSize timelineSize = {800, 200};
    } renderState;

    // Input state
    struct InputState {
        CCPoint mousePos = {0, 0};
        bool isDragging = false;
        int draggedEventIdx = -1;
        float dragStartX = 0.0f;
    } inputState;

    // Initialize UI sections
    void initToolbar();
    void initTimeline();
    void initInspector();
    void initRuler();

    // Rendering
    void updateTimeline(float dt);
    void renderEvents();
    void renderRuler();
    void renderPlayhead();
    void renderInspector();

    // Event handlers
    void onPlayPressed(CCObject*);
    void onPausePressed(CCObject*);
    void onStopPressed(CCObject*);
    void onStepFramePressed(CCObject*);
    void onCBFTogglePressed(CCObject*);
    void onZoomInPressed(CCObject*);
    void onZoomOutPressed(CCObject*);

    // Helper functions
    std::string getButtonName(int button) const;
    ccColor3B getPlayerColor(bool player2) const;
    ccColor3B getActionColor(bool pressed) const;

    // Navigation
    void jumpToEvent(int eventIndex);
    void jumpToFrame(int frame);

    // Scrolling
    void ensureEventVisible(int eventIndex);
    void updateScrollBounds();

    // Update functions
    void updateFrameCounter();
    void updateSubframeCounter();
    void updateInspectorPanel();

    // Schedule updates
    void scheduleUpdate();

    // Cleanup
    ~MacroTimelineLayer() override;
};
