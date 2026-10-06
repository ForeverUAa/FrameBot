#include "macro_timeline_layer.hpp"

#include <Geode/modify/FLAlertLayer.hpp>
#include <limits>


namespace {
    struct FrameTaskResult {
        int frame = 0;
        double subframe = 0.0;
        bool passed = false;
        cocos2d::CCPoint position = {0, 0};
    };

    struct FrameTask {
        int frame = 0;
        double subframe = 0.0;
        int button = 1;
        bool player2 = false;
        bool down = true;
        std::vector<FrameTaskResult> results;
        bool finished = false;
    };

    class FrameTaskPopup final : public geode::Popup<> {
    public:
        static FrameTaskPopup* create(MacroTimeline* timeline) {
            auto ret = new FrameTaskPopup();
            ret->m_timeline = timeline;
            if (ret->initAnchored(520, 400, Utils::getTexture().c_str())) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

    protected:
        bool setup() override {
            if (!Popup::setup())
                return false;

            this->setTitle("Frame Tasks");

            m_list = CCMenu::create();
            m_list->setPosition({0, 0});
            m_mainLayer->addChild(m_list);

            auto addLabel = [&](const char* text, float x, float y, SEL_MenuHandler callback) {
                auto label = CCLabelBMFont::create(text, "bigFont.fnt");
                label->setScale(0.42f);
                auto item = CCMenuItemLabel::create(label, this, callback);
                item->setPosition({x, y});
                m_list->addChild(item);
            };

            addLabel("Add Selected", 90, 32, menu_selector(FrameTaskPopup::onAddSelected));
            addLabel("Start All", 260, 32, menu_selector(FrameTaskPopup::onStartAll));

            m_status = CCLabelBMFont::create("No tasks", "chatFont.fnt");
            m_status->setScale(0.42f);
            m_status->setPosition({260, 65});
            m_mainLayer->addChild(m_status);

            this->schedule(schedule_selector(FrameTaskPopup::updateTaskRunner), 0.016f);
            refreshList();
            return true;
        }

        void onClose(CCObject* sender) override {
            stopTesting(true);
            Popup::onClose(sender);
        }

        void onAddSelected(CCObject*) {
            if (!m_timeline)
                return;

            int index = m_timeline->getSelectedEventIndex();
            const input* event = m_timeline->getEvent(index);
            if (!event)
                return;

            FrameTask task;
            task.frame = event->frame;
            task.subframe = event->subframe;
            task.button = event->button;
            task.player2 = event->player2;
            task.down = event->down;
            s_tasks.push_back(task);

            refreshList();
            m_status->setString(fmt::format("Added task at {}", formatTime(task.frame, task.subframe)).c_str());
        }

        void onStartTask(CCObject* sender) {
            auto item = static_cast<CCNode*>(sender);
            int index = item->getTag();
            if (index >= 0 && index < static_cast<int>(s_tasks.size()))
                startTesting(index, false);
        }

        void onStartAll(CCObject*) {
            if (s_tasks.empty())
                return;

            startTesting(0, true);
        }

        void refreshList() {
            if (!m_list)
                return;

            m_list->removeAllChildren();

            float y = 350.0f;
            for (int i = 0; i < static_cast<int>(s_tasks.size()); ++i) {
                auto& task = s_tasks[i];

                int successful = 0;
                for (const auto& result : task.results)
                    successful += result.passed ? 1 : 0;

                std::string resultText = task.finished
                    ? fmt::format("  {} window{}", successful, successful == 1 ? "" : "s")
                    : "";

                auto text = CCLabelBMFont::create(
                    fmt::format("{}: {}{}", i + 1, formatTime(task.frame, task.subframe), resultText).c_str(),
                    "chatFont.fnt"
                );
                text->setScale(0.38f);
                text->setAnchorPoint({0, 0.5f});
                text->setPosition({45, y});

                auto item = CCMenuItemLabel::create(text, this, menu_selector(FrameTaskPopup::onStartTask));
                item->setTag(i);
                item->setPosition({250, y});
                m_list->addChild(item);

                y -= 28.0f;
                if (y < 90.0f)
                    break;
            }
        }

        static std::string formatTime(int frame, double subframe) {
            if (std::abs(subframe) < 0.0001)
                return std::to_string(frame);

            return fmt::format("{}.{:02d}", frame, static_cast<int>(std::round(subframe * 100.0)));
        }

        void buildCandidates(const FrameTask& task) {
            m_candidates.clear();

            // Probe a symmetric frame window first.
            for (int offset = -8; offset <= 8; ++offset)
                m_candidates.push_back({std::max(0, task.frame + offset), 0.0});

            // If the task itself contains subframe timing, probe between-frame
            // positions as well. This intentionally adds more tests rather
            // than collapsing them to the nearest whole frame.
            if (task.subframe > 0.0001) {
                for (int offset = -2; offset <= 2; ++offset) {
                    for (int step = 1; step < 10; ++step)
                        m_candidates.push_back({
                            std::max(0, task.frame + offset),
                            step / 10.0
                        });
                }
            }
        }

        int findTaskEvent(const Macro& macro, const FrameTask& task) const {
            int best = -1;
            double bestDistance = std::numeric_limits<double>::max();

            for (int i = 0; i < static_cast<int>(macro.inputs.size()); ++i) {
                const auto& event = macro.inputs[i];
                if (event.button != task.button ||
                    event.player2 != task.player2 ||
                    event.down != task.down)
                    continue;

                double distance = std::abs(event.getPreciseFrame() -
                    (task.frame + task.subframe));
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }

            return best;
        }

        void startTesting(int taskIndex, bool all) {
            if (m_testing || taskIndex < 0 || taskIndex >= static_cast<int>(s_tasks.size()))
                return;

            PlayLayer* pl = PlayLayer::get();
            if (!pl || !m_timeline)
                return;

            m_backupMacro = Global::get().macro;
            m_backupState = Global::get().state;
            m_backupCurrentAction = Global::get().currentAction;
            m_backupCurrentFrameFix = Global::get().currentFrameFix;
            m_backupRestart = Global::get().restart;
            m_backupFirstAttempt = Global::get().firstAttempt;
            m_taskIndex = taskIndex;
            m_testingAll = all;
            m_testing = true;

            if (m_testingAll) {
                for (auto& task : s_tasks)
                    task.finished = false;
            } else {
                s_tasks[m_taskIndex].finished = false;
            }

            s_tasks[m_taskIndex].results.clear();
            buildCandidates(s_tasks[m_taskIndex]);
            m_candidateIndex = 0;
            m_targetSeen = false;
            m_attemptStartFrame = 0;

            beginCandidate();
        }

        void beginCandidate() {
            if (!m_testing)
                return;

            if (m_taskIndex < 0 || m_taskIndex >= static_cast<int>(s_tasks.size())) {
                finishTesting();
                return;
            }

            if (m_candidateIndex >= static_cast<int>(m_candidates.size())) {
                finalizeTaskWindow(s_tasks[m_taskIndex]);
                s_tasks[m_taskIndex].finished = true;

                if (m_testingAll && m_taskIndex + 1 < static_cast<int>(s_tasks.size())) {
                    ++m_taskIndex;
                    s_tasks[m_taskIndex].results.clear();
                    buildCandidates(s_tasks[m_taskIndex]);
                    m_candidateIndex = 0;
                    beginCandidate();
                    return;
                }

                finishTesting();
                return;
            }

            PlayLayer* pl = PlayLayer::get();
            if (!pl) {
                finishTesting();
                return;
            }

            auto& task = s_tasks[m_taskIndex];
            auto candidate = m_candidates[m_candidateIndex];

            Macro candidateMacro = m_backupMacro;
            int eventIndex = findTaskEvent(candidateMacro, task);
            if (eventIndex < 0) {
                ++m_candidateIndex;
                beginCandidate();
                return;
            }

            candidateMacro.inputs[eventIndex].frame = candidate.first;
            candidateMacro.inputs[eventIndex].subframe = candidate.second;
            std::sort(candidateMacro.inputs.begin(), candidateMacro.inputs.end());

            // The timing is only considered successful if the player actually
            // gets through the gap to the next input. This mirrors how real
            // frame-window analyzers attribute a death to the shifted input
            // instead of accepting an arbitrary survival lookahead.
            m_passFrame = -1;
            m_passSubframe = 0.0;
            const double candidatePrecise = candidate.first + candidate.second;
            for (const auto& input : candidateMacro.inputs) {
                if (input.getPreciseFrame() > candidatePrecise + 0.0001) {
                    m_passFrame = input.frame;
                    m_passSubframe = input.subframe;
                    break;
                }
            }

            auto& g = Global::get();
            g.macro = candidateMacro;
            g.state = state::playing;
            g.currentAction = 0;
            g.currentFrameFix = 0;
            g.restart = true;
            g.firstAttempt = true;
            g.respawnFrame = -1;

            m_targetFrame = candidate.first;
            m_targetSubframe = candidate.second;
            m_targetSeen = false;
            m_attemptStartFrame = 0;

            pl->resetLevelFromStart();

            m_status->setString(
                fmt::format(
                    "Testing {} / {}: {}",
                    m_candidateIndex + 1,
                    m_candidates.size(),
                    formatTime(m_targetFrame, m_targetSubframe)
                ).c_str()
            );
        }

        void updateTaskRunner(float) {
            if (!m_testing)
                return;

            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return finishTesting();

            int frame = Global::getCurrentFrame();

            if (m_attemptStartFrame == 0 && frame > 0)
                m_attemptStartFrame = frame;

            if (!m_targetSeen && frame >= m_targetFrame) {
                PlayerObject* player = s_tasks[m_taskIndex].player2 ? pl->m_player2 : pl->m_player1;
                if (player) {
                    m_targetPosition = player->getPosition();
                    m_targetSeen = true;
                }
            }

            if (pl->m_player1 && pl->m_player1->m_isDead) {
                recordResult(false);
                return;
            }

            if (pl->m_levelEndAnimationStarted) {
                recordResult(true);
                return;
            }

            // A window ends at the next macro input. Reaching that input
            // means the player made it through the gap being tested. If this
            // is the last input, use a small fallback lookahead because there
            // is no later input to act as the boundary.
            const int passFrame = m_passFrame >= 0
                ? m_passFrame
                : m_targetFrame + 12;

            if (m_targetSeen && frame >= passFrame)
                recordResult(true);
        }

        void recordResult(bool passed) {
            if (!m_testing)
                return;

            auto& task = s_tasks[m_taskIndex];

            FrameTaskResult result;
            result.frame = m_targetFrame;
            result.subframe = m_targetSubframe;
            result.passed = passed;
            result.position = m_targetPosition;
            task.results.push_back(result);

            ++m_candidateIndex;
            beginCandidate();
        }

        void finalizeTaskWindow(FrameTask& task) {
            if (task.results.empty())
                return;

            std::sort(task.results.begin(), task.results.end(), [](const auto& a, const auto& b) {
                if (a.frame != b.frame)
                    return a.frame < b.frame;
                return a.subframe < b.subframe;
            });

            const double center = task.frame + task.subframe;
            int centerIndex = -1;
            double centerDistance = std::numeric_limits<double>::max();

            for (int i = 0; i < static_cast<int>(task.results.size()); ++i) {
                double distance = std::abs(
                    task.results[i].frame + task.results[i].subframe - center
                );
                if (distance < centerDistance) {
                    centerDistance = distance;
                    centerIndex = i;
                }
            }

            // The selected timing is the anchor. A frame is part of the
            // window only while every timing between it and the anchor also
            // passes. A failed gap therefore prevents later, disconnected
            // timings from being counted.
            if (centerIndex < 0 || !task.results[centerIndex].passed) {
                task.results.clear();
                return;
            }

            auto isAdjacent = [](const FrameTaskResult& a, const FrameTaskResult& b) {
                const double delta =
                    (b.frame + b.subframe) - (a.frame + a.subframe);

                // Whole-frame probing uses 1.0. CBF probing uses the smaller
                // subframe step, so accept either a normal frame or a .1
                // subframe increment.
                return std::abs(delta - 1.0) < 0.0001 ||
                       std::abs(delta - 0.1) < 0.0001;
            };

            int first = centerIndex;
            int last = centerIndex;

            while (first > 0 &&
                   task.results[first - 1].passed &&
                   isAdjacent(task.results[first - 1], task.results[first])) {
                --first;
            }

            while (last + 1 < static_cast<int>(task.results.size()) &&
                   task.results[last + 1].passed &&
                   isAdjacent(task.results[last], task.results[last + 1])) {
                ++last;
            }

            std::vector<FrameTaskResult> window(
                task.results.begin() + first,
                task.results.begin() + last + 1
            );

            task.results = std::move(window);

            for (const auto& result : task.results)
                addMarker(result);
        }

        void addMarker(const FrameTaskResult& result) {
            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return;

            constexpr int markerTag = 0xF7A5;

            auto* markerLayer = pl->getChildByTag(markerTag);
            if (!markerLayer) {
                markerLayer = CCLayer::create();
                markerLayer->setTag(markerTag);
                markerLayer->setZOrder(100000);
                pl->addChild(markerLayer);
            }

            auto* draw = CCDrawNode::create();
            draw->drawCircle(
                result.position,
                10.0f,
                ccc4f(0.1f, 1.0f, 0.2f, 0.85f),
                1.5f,
                ccc4f(0.1f, 1.0f, 0.2f, 0.95f),
                24
            );
            markerLayer->addChild(draw);

            auto label = CCLabelBMFont::create(
                formatTime(result.frame, result.subframe).c_str(),
                "chatFont.fnt"
            );
            label->setScale(0.3f);
            label->setAnchorPoint({0.5f, 0.0f});
            label->setPosition({result.position.x, result.position.y + 11.0f});
            markerLayer->addChild(label);
        }

        void finishTesting() {
            if (!m_testing)
                return;

            auto& g = Global::get();
            g.macro = m_backupMacro;
            g.state = m_backupState;
            g.currentAction = m_backupCurrentAction;
            g.currentFrameFix = m_backupCurrentFrameFix;
            g.restart = m_backupRestart;
            g.firstAttempt = m_backupFirstAttempt;

            m_testing = false;

            if (m_testingAll) {
                for (auto& task : s_tasks)
                    task.finished = true;
            } else if (m_taskIndex >= 0 && m_taskIndex < static_cast<int>(s_tasks.size())) {
                s_tasks[m_taskIndex].finished = true;
            }

            Macro::updateTPS();
            Interface::updateLabels();
            Interface::updateButtons();

            refreshList();

            int total = 0;
            for (const auto& task : s_tasks)
                for (const auto& result : task.results)
                    total += result.passed ? 1 : 0;

            m_status->setString(
                fmt::format("Finished: {} successful timings", total).c_str()
            );
        }

        void stopTesting(bool restore) {
            if (!m_testing)
                return;

            if (restore)
                finishTesting();
            else
                m_testing = false;
        }

    private:
        MacroTimeline* m_timeline = nullptr;
        CCMenu* m_list = nullptr;
        CCLabelBMFont* m_status = nullptr;

        bool m_testing = false;
        bool m_testingAll = false;
        int m_taskIndex = -1;
        int m_candidateIndex = 0;
        int m_targetFrame = 0;
        double m_targetSubframe = 0.0;
        int m_passFrame = -1;
        double m_passSubframe = 0.0;
        int m_attemptStartFrame = 0;
        bool m_targetSeen = false;
        CCPoint m_targetPosition = {0, 0};

        Macro m_backupMacro;
        state m_backupState = state::none;
        size_t m_backupCurrentAction = 0;
        size_t m_backupCurrentFrameFix = 0;
        bool m_backupRestart = false;
        bool m_backupFirstAttempt = false;

        std::vector<std::pair<int, double>> m_candidates;

        static inline std::vector<FrameTask> s_tasks;
    };
}

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

    // Tasks menu
    auto taskLabel = CCLabelBMFont::create("Tasks", "bigFont.fnt");
    taskLabel->setScale(0.35f);
    auto taskBtn = CCMenuItemLabel::create(
        taskLabel,
        [this](CCObject*) {
            auto popup = FrameTaskPopup::create(timeline.get());
            if (popup) popup->show();
        }
    );
    taskBtn->setPositionX(240);
    taskBtn->setTag(7);
    toolbarMenu->addChild(taskBtn);

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
    inspectorBg = CCScale9Sprite::create("GJ_square_02_001.png");
    inspectorBg->setContentSize({250, 300});
    inspectorBg->setPosition({this->getContentSize().width - 135, this->getContentSize().height / 2 - 20});
    inspectorBg->setColor({40, 50, 60});
    inspectorBg->setOpacity(220);
    inspectorBg->setZOrder(45);
    this->addChild(inspectorBg);

    auto title = CCLabelBMFont::create("Event Inspector", "bigFont.fnt");
    title->setScale(0.4f);
    title->setPosition({inspectorBg->getContentSize().width / 2, 135});
    inspectorBg->addChild(title);

    inspectorMenu = CCMenu::create();
    inspectorMenu->setPosition({0, 0});
    inspectorBg->addChild(inspectorMenu);

    auto addButton = [&](const char* text, float x, float y, SEL_MenuHandler cb) {
        auto label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(0.35f);
        auto item = CCMenuItemLabel::create(label, this, cb);
        item->setPosition({x, y});
        inspectorMenu->addChild(item);
    };

    addButton("-", 35, 95, menu_selector(MacroTimelineLayer::onFrameDown));
    addButton("+", 215, 95, menu_selector(MacroTimelineLayer::onFrameUp));
    addButton("-", 35, 65, menu_selector(MacroTimelineLayer::onSubframeDown));
    addButton("+", 215, 65, menu_selector(MacroTimelineLayer::onSubframeUp));
    addButton("Button", 125, 35, menu_selector(MacroTimelineLayer::onButtonCycle));
    addButton("Player", 65, 5, menu_selector(MacroTimelineLayer::onPlayerToggle));
    addButton("Action", 185, 5, menu_selector(MacroTimelineLayer::onActionToggle));

    for (int i = 0; i < 8; ++i) {
        inspectorLabels[i] = CCLabelBMFont::create("", "chatFont.fnt");
        inspectorLabels[i]->setScale(0.35f);
        inspectorLabels[i]->setAnchorPoint({0.0f, 0.5f});
        inspectorLabels[i]->setPosition({12, 115 - i * 14});
        inspectorBg->addChild(inspectorLabels[i]);
    }
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
    updateInspectorPanel();
    updateScrollBounds();
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

        // Keep the marker slightly wider than the visual frame so both presses
        // and releases remain easy to select, especially in CBF mode.
        const bool selected = timeline->isEventSelected(i);
        CCScale9Sprite* eventBox = CCScale9Sprite::create("GJ_square_02_001.png");
        eventBox->setScale(1.0f);
        eventBox->setContentSize({8, renderState.eventHeight});
        eventBox->setPosition({x, y});
        eventBox->setColor(color);
        eventBox->setOpacity(selected ? 255 : 180);
        eventBox->setZOrder(selected ? 20 : 10);
        eventsLayer->addChild(eventBox);

        // Add button label (abbreviated)
        CCLabelBMFont* btnLabel = CCLabelBMFont::create(
            std::to_string(evt.button).c_str(),
            "chatFont.fnt"
        );
        btnLabel->setScale(0.25f);
        btnLabel->setPosition({x, y});
        btnLabel->setColor({255, 255, 255});
        btnLabel->setZOrder(selected ? 21 : 11);
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
    if (!inspector->hasData()) {
        for (auto* label : inspectorLabels)
            if (label) label->setVisible(false);
        return;
    }

    for (auto* label : inspectorLabels)
        if (label) label->setVisible(true);
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
    if (!timeline || timeline->getEventCount() == 0) return;
    timeline->setPlayhead(timeline->getPlayheadFrame(), timeline->getPlayheadSubframe());
    this->unschedule(schedule_selector(MacroTimelineLayer::updateTimeline));
    this->schedule(schedule_selector(MacroTimelineLayer::updateTimeline), 1.f / 60.f);
}

void MacroTimelineLayer::onPausePressed(CCObject*) {
    this->unschedule(schedule_selector(MacroTimelineLayer::updateTimeline));
    this->schedule(schedule_selector(MacroTimelineLayer::updateTimeline), 0.016f);
}

void MacroTimelineLayer::onStopPressed(CCObject*) {
    // TODO: Stop playback and reset playhead
    timeline->setPlayhead(0, 0.0);
}

void MacroTimelineLayer::onStepFramePressed(CCObject*) {
    if (!timeline) return;
    timeline->setPlayhead(timeline->getPlayheadFrame() + 1, timeline->getPlayheadSubframe());
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
bool MacroTimelineLayer::onTouchBegan(CCTouch* touch, CCEvent*) {
    if (!timeline) return false;

    const CCPoint layerPos = this->convertToNodeSpace(touch->getLocation());
    const CCPoint eventPos = eventsLayer->convertToNodeSpace(touch->getLocation());
    int eventIdx = hitTestEvent(eventPos);
    if (eventIdx >= 0) {
        timeline->selectEvent(eventIdx);
        inputState.isDragging = true;
        inputState.draggedEventIdx = eventIdx;
        inputState.dragStartX = eventPos.x;
        inputState.dragStartPreciseFrame = timeline->getEvent(eventIdx)->getPreciseFrame();
        return true;
    }

    // Clicking the timeline ruler moves the playhead.
    const float timelineTop = this->getContentSize().height - 60.0f;
    if (layerPos.x >= 10.0f && layerPos.x <= 10.0f + renderState.timelineSize.width &&
        layerPos.y >= timelineTop - renderState.timelineSize.height &&
        layerPos.y <= timelineTop) {
        float localX = layerPos.x - 10.0f;
        timeline->setPlayheadPrecise(
            (localX + timeline->getScrollOffset()) / renderState.pixelsPerFrame
        );
        return true;
    }

    return false;
}

void MacroTimelineLayer::onTouchMoved(CCTouch* touch, CCEvent*) {
    if (!inputState.isDragging || inputState.draggedEventIdx < 0) return;

    const CCPoint pos = eventsLayer->convertToNodeSpace(touch->getLocation());
    double precise = (pos.x + timeline->getScrollOffset()) / renderState.pixelsPerFrame;

    if (precise < 0.0) precise = 0.0;
    timeline->setEventFrame(inputState.draggedEventIdx, static_cast<int>(precise));
    if (timeline->isCBFModeEnabled())
        timeline->setEventSubframe(inputState.draggedEventIdx, precise - std::floor(precise));
}

void MacroTimelineLayer::onTouchEnded(CCTouch*, CCEvent*) {
    inputState.isDragging = false;
    inputState.draggedEventIdx = -1;
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
    return pressed ? ccc3(100, 255, 100) : ccc3(255, 100, 100);
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
    if (!inspector || !inspector->hasData()) return;

    const auto& d = inspector->getDetails();
    const std::string values[] = {
        fmt::format("Frame: {}", d.frame),
        fmt::format("Subframe: {:.2f}", d.subframe),
        fmt::format("Button: {} ({})", d.button, getButtonName(d.button)),
        fmt::format("Player: {}", d.player2 ? "P2" : "P1"),
        fmt::format("Action: {}", d.pressed ? "Press" : "Release"),
        fmt::format("Index: {}", d.index),
        fmt::format("Previous: {}", d.prevEventIndex >= 0 ? fmt::format("{} @ {}", d.prevEventIndex, d.prevEventFrame) : "None"),
        fmt::format("Next: {}", d.nextEventIndex >= 0 ? fmt::format("{} @ {}", d.nextEventIndex, d.nextEventFrame) : "None")
    };

    for (int i = 0; i < 8; ++i)
        inspectorLabels[i]->setString(values[i].c_str());
}

void MacroTimelineLayer::adjustSelectedFrame(int delta) {
    if (!inspector->hasData()) return;
    inspector->setFrame(std::max(0, inspector->getDetails().frame + delta));
}

void MacroTimelineLayer::adjustSelectedSubframe(double delta) {
    if (!inspector->hasData()) return;
    inspector->setSubframe(inspector->getDetails().subframe + delta);
}

void MacroTimelineLayer::cycleSelectedButton() {
    if (!inspector->hasData()) return;
    int button = inspector->getDetails().button;
    inspector->setButton(button >= 3 ? 1 : button + 1);
}

void MacroTimelineLayer::toggleSelectedPlayer() {
    if (!inspector->hasData()) return;
    inspector->setPlayer(!inspector->getDetails().player2);
}

void MacroTimelineLayer::toggleSelectedAction() {
    if (!inspector->hasData()) return;
    inspector->setPressed(!inspector->getDetails().pressed);
}

void MacroTimelineLayer::onFrameDown(CCObject*) { adjustSelectedFrame(-1); }
void MacroTimelineLayer::onFrameUp(CCObject*) { adjustSelectedFrame(1); }
void MacroTimelineLayer::onSubframeDown(CCObject*) { adjustSelectedSubframe(-0.01); }
void MacroTimelineLayer::onSubframeUp(CCObject*) { adjustSelectedSubframe(0.01); }
void MacroTimelineLayer::onButtonCycle(CCObject*) { cycleSelectedButton(); }
void MacroTimelineLayer::onPlayerToggle(CCObject*) { toggleSelectedPlayer(); }
void MacroTimelineLayer::onActionToggle(CCObject*) { toggleSelectedAction(); }
