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

class MacroTimelineLayer : public framebot::Popup<Macro*> {
public:
    static MacroTimelineLayer* create(Macro* macro);

    // Touch event handlers (public for modifier access)
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override;
    void ccTouchMoved(CCTouch* touch, CCEvent* event) override;
    void ccTouchEnded(CCTouch* touch, CCEvent* event) override;

private:
    bool setup(Macro* macro) override;
    void keyBackClicked() override;

    Macro* macro = nullptr;
    std::unique_ptr<MacroTimeline> timeline;
    std::unique_ptr<MacroEventInspector> inspector;

    // UI Components
    CCMenu* timelineMenu = nullptr;
    CCNode* overlay = nullptr;         // screen-space root for the whole overlay
    CCMenu* toolbarMenu = nullptr;
    CCLayer* timelineLayer = nullptr;
    CCLayer* rulerLayer = nullptr;     // Timeline ruler/frame counter
    CCLayer* eventsLayer = nullptr;    // Event rendering
    CCLayer* cursorLayer = nullptr;    // Playhead cursor

    // UI Elements
    CCLabelBMFont* frameCounterLabel = nullptr;
    CCLabelBMFont* subframeLabel = nullptr;
    CCLabelBMFont* timelineInfoLabel = nullptr;
    CCLabelBMFont* inspectorLabels[8] = {};
    CCMenu* inspectorMenu = nullptr;
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
        int lastVisibleFrame = 160;
        CCSize timelineSize = {500.0f, 330.0f};
    } renderState;

    // Input state
    struct InputState {
        CCPoint mousePos = {0, 0};
        bool isDragging = false;
        int draggedEventIdx = -1;
        float dragStartX = 0.0f;
        double dragStartPreciseFrame = 0.0;
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

    void onFrameDown(CCObject*);
    void onFrameUp(CCObject*);
    void onSubframeDown(CCObject*);
    void onSubframeUp(CCObject*);
    void onButtonCycle(CCObject*);
    void onPlayerToggle(CCObject*);
    void onActionToggle(CCObject*);
    void onTasksPressed(CCObject*);
    void onUndoPressed(CCObject*);
    void onRedoPressed(CCObject*);

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
    void adjustSelectedFrame(int delta);
    void adjustSelectedSubframe(double delta);
    void cycleSelectedButton();
    void toggleSelectedPlayer();
    void toggleSelectedAction();

    int hitTestEvent(const CCPoint& pos);
    CCPoint getEventRenderPos(int eventIndex);
    CCRect getEventRenderRect(int eventIndex);
    float frameToPixels(int frame, double subframe = 0.0) const;
    int pixelsToFrame(float pixels) const;

    // Schedule updates
    void scheduleUpdate();

    // Cleanup
    ~MacroTimelineLayer() override;
};
