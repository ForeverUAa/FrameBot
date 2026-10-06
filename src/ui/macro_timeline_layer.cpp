#include "macro_timeline_layer.hpp"

#include <Geode/modify/FLAlertLayer.hpp>

// Touch handler for MacroTimelineLayer
#ifdef GEODE_IS_WINDOWS
class $modify(FLAlertLayer) {
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        if (!FLAlertLayer::ccTouchBegan(touch, event)) return false;

        MacroTimelineLayer* layer = typeinfo_cast<MacroTimelineLayer*>(this);
        if (!layer) return true;

        return layer->onTouchBegan(touch, event);
    }

    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        FLAlertLayer::ccTouchMoved(touch, event);

        MacroTimelineLayer* layer = typeinfo_cast<MacroTimelineLayer*>(this);
        if (!layer) return;

        layer->onTouchMoved(touch, event);
    }

    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override {
        FLAlertLayer::ccTouchEnded(touch, event);

        MacroTimelineLayer* layer = typeinfo_cast<MacroTimelineLayer*>(this);
        if (!layer) return;

        layer->onTouchEnded(touch, event);
    }
};
#endif

MacroTimelineLayer* MacroTimelineLayer::create(Macro* macro) {
    MacroTimelineLayer* ret = new MacroTimelineLayer();
    if (ret->initAnchored(900, 600, Utils::getTexture().c_str())) {
        ret->macro = macro;
        ret->timeline = std::make_unique<MacroTimeline>(macro);
        ret->inspector = std::make_unique<MacroEventInspector>(ret->timeline.get());
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

MacroTimelineLayer::~MacroTimelineLayer() {}

bool MacroTimelineLayer::setup() {
    if (!Popup::setup()) return false;

    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    this->registerWithTouchDispatcher();

    // Set up main content
    initToolbar();
    initTimeline();
    initInspector();

    scheduleUpdate();
    return true;
}

void MacroTimelineLayer::keyBackClicked() {
    this->onClose(nullptr);
}

void MacroTimelineLayer::initToolbar() {
    // Create toolbar menu
    toolbarMenu = CCMenu::create();
    toolbarMenu->setAnchorPoint({0.5f, 1.0f});
    toolbarMenu->setPosition({this->getContentSize().width / 2, this->getContentSize().height - 10});
    toolbarMenu->setZOrder(100);
    this->addChild(toolbarMenu);

    // Play button
    CCSprite* spr = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
    playBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MacroTimelineLayer::onPlayPressed));
    playBtn->setScale(0.6f);
    playBtn->setTag(0);
    toolbarMenu->addChild(playBtn);

    // Pause button
    spr = CCSprite::createWithSpriteFrameName("GJ_pauseBtn_001.png");
    pauseBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MacroTimelineLayer::onPausePressed));
    pauseBtn->setScale(0.6f);
    pauseBtn->setPositionX(30);
    pauseBtn->setTag(1);
    toolbarMenu->addChild(pauseBtn);

    // Stop button
    spr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
    stopBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MacroTimelineLayer::onStopPressed));
    stopBtn->setScale(0.6f);
    stopBtn->setPositionX(60);
    stopBtn->setTag(2);
    toolbarMenu->addChild(stopBtn);

    // Step frame button
    spr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    stepFrameBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MacroTimelineLayer::onStepFramePressed));
    stepFrameBtn->setScale(0.6f);
    stepFrameBtn->setPositionX(90);
    stepFrameBtn->setTag(3);
    toolbarMenu->addChild(stepFrameBtn);

    // CBF mode toggle
    spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    cbfModeToggle = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MacroTimelineLayer::onCBFTogglePressed));
    cbfModeToggle->setScale(0.6f);
    cbfModeToggle->setPositionX(130);
    cbfModeToggle->setTag(4);
    toolbarMenu->addChild(cbfModeToggle);

    // Zoom buttons
    spr = CCSprite::createWithSpriteFrameName("edit_rightBtn_001.png");
    zoomInBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MacroTimelineLayer::onZoomInPressed));
    zoomInBtn->setScale(0.6f);
    zoomInBtn->setPositionX(170);
    zoomInBtn->setTag(5);
    toolbarMenu->addChild(zoomInBtn);

    spr = CCSprite::createWithSpriteFrameName("edit_leftBtn_001.png");
    zoomOutBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MacroTimelineLayer::onZoomOutPressed));
    zoomOutBtn->setScale(0.6f);
    zoomOutBtn->setPositionX(200);
    zoomOutBtn->setTag(6);
    toolbarMenu->addChild(zoomOutBtn);
}

void MacroTimelineLayer::initTimeline() {
    // Create main timeline container
    timelineLayer = CCLayer::create();
    timelineLayer->setAnchorPoint({0, 1});
    timelineLayer->setPosition({10, this->getContentSize().height - 60});
    timelineLayer->setContentSize(renderState.timelineSize);
    timelineLayer->setZOrder(50);
    this->addChild(timelineLayer);

    // Create ruler
    rulerLayer = CCLayer::create();
    rulerLayer->setAnchorPoint({0, 0});
    rulerLayer->setPosition({0, 0});
    rulerLayer->setContentSize({renderState.timelineSize.width, renderState.rulerHeight});
    timelineLayer->addChild(rulerLayer);

    // Create events rendering layer
    eventsLayer = CCLayer::create();
    eventsLayer->setAnchorPoint({0, 0});
    eventsLayer->setPosition({0, -renderState.rulerHeight});
    eventsLayer->setContentSize({renderState.timelineSize.width, renderState.timelineSize.height - renderState.rulerHeight});
    timelineLayer->addChild(eventsLayer);

    // Create cursor layer for playhead
    cursorLayer = CCLayer::create();
    cursorLayer->setAnchorPoint({0, 0});
    cursorLayer->setPosition({0, -renderState.rulerHeight});
    cursorLayer->setContentSize({renderState.timelineSize.width, renderState.timelineSize.height - renderState.rulerHeight});
    cursorLayer->setZOrder(10);
    timelineLayer->addChild(cursorLayer);

    // Background for timeline
    CCScale9Sprite* bg = CCScale9Sprite::create("GJ_square_02_001.png");
    bg->setScale(1.0f);
    bg->setContentSize(renderState.timelineSize);
    bg->setPosition(renderState.timelineSize / 2);
    bg->setColor({40, 40, 40});
    bg->setOpacity(200);
    timelineLayer->addChildAtPosition(bg, geode::Anchor::Center);

    // Labels for frame counter
    frameCounterLabel = CCLabelBMFont::create("Frame: 0", "chatFont.fnt");
    frameCounterLabel->setScale(0.5f);
    frameCounterLabel->setAnchorPoint({0, 1});
    frameCounterLabel->setPosition({10, this->getContentSize().height - 45});
    frameCounterLabel->setZOrder(60);
    this->addChild(frameCounterLabel);

    subframeLabel = CCLabelBMFont::create("Subframe: 0%", "chatFont.fnt");
    subframeLabel->setScale(0.5f);
    subframeLabel->setAnchorPoint({0, 1});
    subframeLabel->setPosition({100, this->getContentSize().height - 45});
    subframeLabel->setZOrder(60);
    this->addChild(subframeLabel);
}

void MacroTimelineLayer::initInspector() {
    // Create inspector panel on the right side
    inspectorBg = CCScale9Sprite::create("GJ_square_02_001.png");
    inspectorBg->setScale(1.0f);
    inspectorBg->setContentSize({250, 200});
    inspectorBg->setPosition({this->getContentSize().width - 135, this->getContentSize().height / 2});
    inspectorBg->setColor({40, 50, 60});
    inspectorBg->setOpacity(220);
    inspectorBg->setZOrder(45);
    this->addChild(inspectorBg);

    CCLabelBMFont* lbl = CCLabelBMFont::create("Event Inspector", "bigFont.fnt");
    lbl->setScale(0.4f);
    lbl->setAnchorPoint({0.5f, 1});
    lbl->setPosition({inspectorBg->getPositionX(), inspectorBg->getPositionY() + 95});
    this->addChild(lbl);
}

void MacroTimelineLayer::updateTimeline(float dt) {
    if (!timeline) return;

    inspector->updateFromTimeline();
    updateFrameCounter();
    updateSubframeCounter();
    renderRuler();
    renderEvents();
    renderPlayhead();
    renderInspector();
}

void MacroTimelineLayer::renderEvents() {
    eventsLayer->removeAllChildren();

    if (!macro || macro->inputs.empty()) return;

    const int p1Y = 10;
    const int p2Y = renderState.rulerHeight + 10;

    for (int i = 0; i < macro->inputs.size(); i++) {
        const auto& evt = macro->inputs[i];
        float x = frameToPixels(evt.frame, evt.subframe);

        // Skip if outside visible area
        if (x < 0 || x > renderState.timelineSize.width) continue;

        int y = evt.player2 ? p2Y : p1Y;
        ccColor3B color = getActionColor(evt.down);

        // Draw event as small rectangle
        CCScale9Sprite* eventBox = CCScale9Sprite::create("GJ_square_02_001.png");
        eventBox->setScale(1.0f);
        eventBox->setContentSize({6, renderState.eventHeight});
        eventBox->setPosition({x, y});
        eventBox->setColor(color);
        eventBox->setOpacity(timeline->isEventSelected(i) ? 255 : 180);
        eventBox->setZOrder(timeline->isEventSelected(i) ? 20 : 10);
        eventsLayer->addChild(eventBox);

        // Add button label (abbreviated)
        CCLabelBMFont* btnLabel = CCLabelBMFont::create(
            std::to_string(evt.button).c_str(),
            "chatFont.fnt"
        );
        btnLabel->setScale(0.25f);
        btnLabel->setPosition({x, y});
        btnLabel->setColor({255, 255, 255});
        eventsLayer->addChild(btnLabel);
    }
}

void MacroTimelineLayer::renderRuler() {
    rulerLayer->removeAllChildren();

    // Draw frame numbers
    int frameStep = 60;  // Draw numbers every 60 frames

    for (int f = renderState.firstVisibleFrame; f <= renderState.lastVisibleFrame; f += frameStep) {
        float x = frameToPixels(f);

        if (x < 0 || x > renderState.timelineSize.width) continue;

        // Draw tick mark as small vertical line using sprite
        CCSprite* tick = CCSprite::createWithSpriteFrameName("pixel.png");
        if (!tick) {
            // Fallback: create a 1x10 colored rectangle
            tick = CCSprite::create();
            tick->setScaleX(0.1f);
            tick->setScaleY(2.0f);
        }
        tick->setPosition({x, 10});
        tick->setColor({200, 200, 200});
        tick->setOpacity(180);
        rulerLayer->addChild(tick);

        // Draw label
        CCLabelBMFont* label = CCLabelBMFont::create(
            std::to_string(f).c_str(),
            "chatFont.fnt"
        );
        label->setScale(0.3f);
        label->setAnchorPoint({0.5f, 1});
        label->setPosition({x, 20});
        label->setColor({200, 200, 200});
        rulerLayer->addChild(label);
    }
}

void MacroTimelineLayer::renderPlayhead() {
    cursorLayer->removeAllChildren();

    float x = frameToPixels(timeline->getPlayheadFrame(), timeline->getPlayheadSubframe());

    if (x >= 0 && x <= renderState.timelineSize.width) {
        // Create a vertical line representing the playhead
        CCSprite* cursor = CCSprite::createWithSpriteFrameName("pixel.png");
        if (!cursor) {
            cursor = CCSprite::create();
            cursor->setScaleX(0.5f);
            cursor->setScaleY(10.0f);
        }
        cursor->setPosition({x, renderState.timelineSize.height / 2});
        cursor->setColor({255, 100, 100});
        cursor->setOpacity(220);
        cursor->setZOrder(100);
        cursorLayer->addChild(cursor);
    }
}

void MacroTimelineLayer::renderInspector() {
    if (!inspector->hasData()) return;

    const auto& details = inspector->getDetails();

    // Update inspector labels (simplified for now)
    // In full implementation, this would create proper UI elements
}

void MacroTimelineLayer::updateFrameCounter() {
    int frame = timeline->getPlayheadFrame();
    frameCounterLabel->setString(fmt::format("Frame: {}", frame).c_str());
}

void MacroTimelineLayer::updateSubframeCounter() {
    double subframe = timeline->getPlayheadSubframe();
    int percent = static_cast<int>(subframe * 100);
    subframeLabel->setString(fmt::format("Subframe: {}%", percent).c_str());
}

void MacroTimelineLayer::scheduleUpdate() {
    this->schedule(schedule_selector(MacroTimelineLayer::updateTimeline), 0.016f);  // ~60 FPS
}

// Event handlers
void MacroTimelineLayer::onPlayPressed(CCObject*) {
    // TODO: Connect to Global::macro playback
}

void MacroTimelineLayer::onPausePressed(CCObject*) {
    // TODO: Pause playback
}

void MacroTimelineLayer::onStopPressed(CCObject*) {
    // TODO: Stop playback and reset playhead
    timeline->setPlayhead(0, 0.0);
}

void MacroTimelineLayer::onStepFramePressed(CCObject*) {
    // TODO: Step one frame forward
}

void MacroTimelineLayer::onCBFTogglePressed(CCObject*) {
    timeline->toggleCBFMode();
}

void MacroTimelineLayer::onZoomInPressed(CCObject*) {
    timeline->zoom(25);
    renderState.pixelsPerFrame = timeline->getZoomLevel() / 100.0f * 4.0f;
}

void MacroTimelineLayer::onZoomOutPressed(CCObject*) {
    timeline->zoom(-25);
    renderState.pixelsPerFrame = timeline->getZoomLevel() / 100.0f * 4.0f;
}

// Input handling
bool MacroTimelineLayer::onTouchBegan(CCTouch* touch, CCEvent* event) {
    return true;
}

void MacroTimelineLayer::onTouchMoved(CCTouch* touch, CCEvent* event) {}

void MacroTimelineLayer::onTouchEnded(CCTouch* touch, CCEvent* event) {
    CCPoint pos = touch->getLocation();
    int eventIdx = hitTestEvent(pos);
    if (eventIdx >= 0) {
        timeline->selectEvent(eventIdx);
    }
}

// Helper functions
int MacroTimelineLayer::hitTestEvent(const CCPoint& pos) {
    for (int i = 0; i < timeline->getEventCount(); i++) {
        CCRect rect = getEventRenderRect(i);
        if (rect.containsPoint(pos)) {
            return i;
        }
    }
    return -1;
}

CCPoint MacroTimelineLayer::getEventRenderPos(int eventIndex) {
    if (eventIndex < 0 || eventIndex >= timeline->getEventCount()) return {0, 0};

    const auto& evt = timeline->getEvent(eventIndex);
    if (!evt) return {0, 0};

    float x = frameToPixels(evt->frame, evt->subframe);
    float y = evt->player2 ? 50.0f : 10.0f;

    return {x, y};
}

CCRect MacroTimelineLayer::getEventRenderRect(int eventIndex) {
    auto pos = getEventRenderPos(eventIndex);
    return CCRectMake(pos.x - 3, pos.y - 8, 6, 16);
}

float MacroTimelineLayer::frameToPixels(int frame, double subframe) const {
    return (frame + subframe) * renderState.pixelsPerFrame - timeline->getScrollOffset();
}

int MacroTimelineLayer::pixelsToFrame(float pixels) const {
    float framePos = (pixels + timeline->getScrollOffset()) / renderState.pixelsPerFrame;
    return static_cast<int>(framePos);
}

std::string MacroTimelineLayer::getButtonName(int button) const {
    const std::string names[] = {"None", "Jump", "Left", "Right"};
    if (button >= 0 && button < 4) return names[button];
    return "?";
}

ccColor3B MacroTimelineLayer::getPlayerColor(bool player2) const {
    return player2 ? ccc3(100, 150, 255) : ccc3(255, 100, 100);
}

ccColor3B MacroTimelineLayer::getActionColor(bool pressed) const {
    return pressed ? ccc3(100, 255, 100) : ccc3(255, 150, 100);
}

void MacroTimelineLayer::jumpToEvent(int eventIndex) {
    const input* evt = timeline->getEvent(eventIndex);
    if (evt) {
        timeline->setPlayhead(evt->frame, evt->subframe);
    }
}

void MacroTimelineLayer::jumpToFrame(int frame) {
    timeline->setPlayhead(frame, 0.0);
}

void MacroTimelineLayer::ensureEventVisible(int eventIndex) {
    if (eventIndex < 0 || eventIndex >= timeline->getEventCount()) return;
    const input* evt = timeline->getEvent(eventIndex);
    if (!evt) return;

    float x = frameToPixels(evt->frame, evt->subframe);
    float padding = 50.0f;

    if (x < padding) {
        timeline->scroll(-(int)(x - padding));
    } else if (x > renderState.timelineSize.width - padding) {
        timeline->scroll((int)(x - renderState.timelineSize.width + padding));
    }
}

void MacroTimelineLayer::updateScrollBounds() {
    // Ensure playhead is visible during playback
    float playheadX = frameToPixels(timeline->getPlayheadFrame(), timeline->getPlayheadSubframe());
    if (playheadX < 0 || playheadX > renderState.timelineSize.width) {
        timeline->setScrollOffset((int)(playheadX - renderState.timelineSize.width / 2));
    }
}

void MacroTimelineLayer::updateInspectorPanel() {
    if (!inspector->hasData()) return;

    const auto& details = inspector->getDetails();
    // TODO: Update inspector UI with event details
}
