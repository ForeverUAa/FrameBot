#include "macro_timeline_layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "../hacks/cbf.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>

namespace {
    enum class AlignmentKind {
        Orb,
        Portal
    };

    struct AlignmentSample {
        AlignmentKind kind = AlignmentKind::Orb;
        int objectId = 0;
        int inputFrame = 0;
        double inputSubframe = 0.0;
        int interactionFrame = 0;
        cocos2d::CCPoint playerPosition = {0, 0};
        cocos2d::CCPoint objectPosition = {0, 0};
        float axisDelta = 0.0f;
    };

    struct FrameTaskResult {
        int frame = 0;
        double subframe = 0.0;
        bool survived = false;
        bool died = false;
        cocos2d::CCPoint position = {0, 0};
        std::vector<AlignmentSample> alignments;
    };

    struct FrameTask {
        int eventIndex = -1;
        int frame = 0;
        double subframe = 0.0;
        int button = 1;
        bool player2 = false;
        bool down = true;
        std::vector<FrameTaskResult> results;
        std::vector<AlignmentSample> alignments;
        bool finished = false;
        int tested = 0;
    };

    struct AlignmentProbe {
        bool active = false;
        bool player2 = false;
        int firstFrame = 0;
        int lastFrame = 0;
        int inputFrame = 0;
        double inputSubframe = 0.0;
        std::vector<AlignmentSample> hits;

        void reset() {
            active = false;
            hits.clear();
        }

        void record(
            PlayerObject* player,
            GameObject* object,
            AlignmentKind kind
        ) {
            if (!active || !player || !object)
                return;

            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return;

            PlayerObject* expected = player2 ? pl->m_player2 : pl->m_player1;
            if (!expected || player != expected)
                return;

            const int frame = Global::getCurrentFrame();
            if (frame < firstFrame || frame > lastFrame)
                return;

            const auto playerPosition = player->getPosition();
            const auto objectPosition = object->getPosition();
            const float delta = kind == AlignmentKind::Orb
                ? playerPosition.y - objectPosition.y
                : playerPosition.x - objectPosition.x;

            // Collision callbacks can fire more than once for one object in a run.
            auto existing = std::find_if(
                hits.begin(),
                hits.end(),
                [&](const AlignmentSample& sample) {
                    return sample.kind == kind &&
                        sample.objectId == object->m_objectID &&
                        std::abs(sample.objectPosition.x - objectPosition.x) < 0.1f &&
                        std::abs(sample.objectPosition.y - objectPosition.y) < 0.1f;
                }
            );
            if (existing != hits.end())
                return;

            hits.push_back({
                kind,
                object->m_objectID,
                inputFrame,
                inputSubframe,
                frame,
                playerPosition,
                objectPosition,
                delta
            });
        }
    };

    AlignmentProbe g_alignmentProbe;

    bool isPortalAlignmentObject(int objectId) {
        // Gravity, mode, size, speed, and later-game mode portals.
        // Filtering IDs prevents unrelated effect triggers from being reported.
        static const std::unordered_set<int> ids = {
            10, 11, 12, 13, 47,
            99, 101, 111,
            200, 201, 202, 203,
            286, 287,
            660, 745,
            1331, 1334,
            1931, 1932
        };
        return ids.contains(objectId);
    }

    void captureOrbAlignment(PlayerObject* player, RingObject* ring) {
        g_alignmentProbe.record(player, ring, AlignmentKind::Orb);
    }

    void capturePortalAlignment(PlayerObject* player, EffectGameObject* portal) {
        if (!portal || !isPortalAlignmentObject(portal->m_objectID))
            return;

        g_alignmentProbe.record(player, portal, AlignmentKind::Portal);
    }
}

class $modify(GJBaseGameLayer) {
    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        captureOrbAlignment(player, ring);
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        capturePortalAlignment(player, object);
        GJBaseGameLayer::playerTouchedTrigger(player, object);
    }
};

namespace {
    class FrameTaskPopup final : public framebot::Popup<> {
    public:
        static constexpr int markerTag = 0xF7A5;

        static FrameTaskPopup* create(MacroTimeline* timeline) {
            auto* ret = new FrameTaskPopup();
            ret->m_timeline = timeline;
            if (ret->initAnchored(520, 320, Utils::getTexture().c_str())) {
                ret->autorelease();
                return ret;
            }

            delete ret;
            return nullptr;
        }

    protected:
        bool setup() override {
            setTitle("ALIGNMENT ANALYZER");

            auto panel = [&](CCRect rect, ccColor3B color, GLubyte opacity) {
                auto* bg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
                bg->setContentSize(rect.size);
                bg->setPosition(rect.origin + rect.size / 2.0f);
                bg->setColor(color);
                bg->setOpacity(opacity);
                m_mainLayer->addChild(bg);
                return bg;
            };

            panel({10, 40, 500, 232}, {18, 20, 25}, 235);
            panel({16, 234, 488, 36}, {28, 32, 42}, 245);
            panel({16, 44, 488, 178}, {10, 12, 16}, 210);

            auto* accent = CCDrawNode::create();
            accent->drawRect(
                CCRectMake(16, 268, 488, 2),
                ccc4f(0.25f, 0.75f, 1.0f, 0.9f),
                0.0f,
                ccc4f(0, 0, 0, 0)
            );
            m_mainLayer->addChild(accent);

            m_headerLabel = CCLabelBMFont::create("ALIGNMENT ANALYZER", "bigFont.fnt");
            m_headerLabel->setScale(0.46f);
            m_headerLabel->setAnchorPoint({0, 0.5f});
            m_headerLabel->setPosition({22, 252});
            m_mainLayer->addChild(m_headerLabel);

            m_countLabel = CCLabelBMFont::create("0 INPUTS", "chatFont.fnt");
            m_countLabel->setScale(0.38f);
            m_countLabel->setAnchorPoint({1, 0.5f});
            m_countLabel->setPosition({496, 252});
            m_countLabel->setOpacity(190);
            m_mainLayer->addChild(m_countLabel);

            m_selectedLabel = CCLabelBMFont::create("Select an input on the timeline", "bigFont.fnt");
            m_selectedLabel->setScale(0.39f);
            m_selectedLabel->setAnchorPoint({0, 0.5f});
            m_selectedLabel->setPosition({22, 231});
            m_mainLayer->addChild(m_selectedLabel);

            m_list = CCMenu::create();
            m_list->setPosition({0, 0});
            m_mainLayer->addChild(m_list, 5);

            m_status = CCLabelBMFont::create(
                "Ready. Task scans nearby timings; Analyze scans the macro.",
                "chatFont.fnt"
            );
            m_status->setScale(0.32f);
            m_status->setAnchorPoint({0, 0.5f});
            m_status->setPosition({22, 80});
            m_mainLayer->addChild(m_status);

            m_detailsLabel = CCLabelBMFont::create(
                "Orb: player Y - orb Y     |     Portal: player X - portal X",
                "chatFont.fnt"
            );
            m_detailsLabel->setScale(0.28f);
            m_detailsLabel->setAnchorPoint({0, 0.5f});
            m_detailsLabel->setPosition({22, 59});
            m_detailsLabel->setOpacity(200);
            m_mainLayer->addChild(m_detailsLabel);

            m_actionMenu = CCMenu::create();
            m_actionMenu->setPosition({0, 0});
            m_mainLayer->addChild(m_actionMenu, 10);

            struct Action {
                const char* text;
                SEL_MenuHandler callback;
            };

            const std::array<Action, 4> actions = {{
                {"TASK", menu_selector(FrameTaskPopup::onTask)},
                {"ANALYZE", menu_selector(FrameTaskPopup::onAnalyze)},
                {"STOP", menu_selector(FrameTaskPopup::onStop)},
                {"CLEAR", menu_selector(FrameTaskPopup::onClear)}
            }};

            constexpr float gap = 12.0f;
            float naturalWidth = 0.0f;
            std::array<ButtonSprite*, actions.size()> sprites{};
            for (size_t i = 0; i < actions.size(); ++i) {
                sprites[i] = ButtonSprite::create(actions[i].text);
                naturalWidth += sprites[i]->getContentSize().width;
            }

            const float available = 450.0f - gap * (actions.size() - 1);
            const float scale = std::min(0.46f, available / std::max(1.0f, naturalWidth));
            const float total = naturalWidth * scale + gap * (actions.size() - 1);
            float x = 260.0f - total / 2.0f;

            for (size_t i = 0; i < actions.size(); ++i) {
                sprites[i]->setScale(scale);
                auto* item = CCMenuItemSpriteExtra::create(
                    sprites[i],
                    this,
                    actions[i].callback
                );
                const float width = sprites[i]->getContentSize().width * scale;
                item->setPosition({x + width / 2.0f, 20});
                m_actionMenu->addChild(item);
                x += width + gap;
            }

            if (m_timeline) {
                const int selected = m_timeline->getSelectedEventIndex();
                if (selected >= 0 && selected < m_timeline->getEventCount()) {
                    s_tasks.clear();
                    s_tasks.push_back(makeTask(selected));
                    m_selectedTaskIndex = 0;
                }
            }

            schedule(schedule_selector(FrameTaskPopup::updateTaskRunner), 0.016f);
            refreshList();
            if (m_selectedTaskIndex >= 0)
                selectTask(m_selectedTaskIndex);

            return true;
        }

        void onClose(CCObject* sender) override {
            cancelTesting();
            Popup::onClose(sender);
        }

        void selectTask(int index) {
            if (index < 0 || index >= static_cast<int>(s_tasks.size()))
                return;

            m_selectedTaskIndex = index;
            const auto& task = s_tasks[index];

            if (m_timeline)
                m_timeline->selectEvent(task.eventIndex);

            if (m_selectedLabel) {
                m_selectedLabel->setString(
                    fmt::format(
                        "INPUT {}  @  {}  |  {}{}",
                        index + 1,
                        formatTime(task.frame, task.subframe),
                        task.player2 ? "P2" : "P1",
                        task.down ? " PRESS" : " RELEASE"
                    ).c_str()
                );
            }

            if (m_status) {
                if (m_testing && index == m_taskIndex) {
                    m_status->setString(
                        fmt::format(
                            "Scanning input {}: {} timing samples tested",
                            index + 1,
                            task.tested
                        ).c_str()
                    );
                } else if (!task.finished) {
                    m_status->setString(
                        fmt::format(
                            "Ready. Task scans ±6 frames; Analyze scans ±24. Current input: {}",
                            formatTime(task.frame, task.subframe)
                        ).c_str()
                    );
                } else {
                    m_status->setString(
                        fmt::format(
                            "Finished: {} timings checked, {} distinct alignments",
                            task.tested,
                            task.alignments.size()
                        ).c_str()
                    );
                }
            }

            updateDetails(task);
            refreshList();
        }

        FrameTask makeTask(int index) const {
            FrameTask task;
            if (!m_timeline)
                return task;

            const input* event = m_timeline->getEvent(index);
            if (!event)
                return task;

            task.eventIndex = index;
            task.frame = event->frame;
            task.subframe = event->subframe;
            task.button = event->button;
            task.player2 = event->player2;
            task.down = event->down;
            return task;
        }

        void onTask(CCObject*) {
            if (m_testing || !m_timeline)
                return;

            int index = m_selectedTaskIndex;
            if (index < 0 || index >= static_cast<int>(s_tasks.size())) {
                index = m_timeline->getSelectedEventIndex();
                FrameTask task = makeTask(index);
                if (task.eventIndex < 0) {
                    m_status->setString("Select an input on the timeline first.");
                    return;
                }

                s_tasks.push_back(task);
                index = static_cast<int>(s_tasks.size()) - 1;
                m_selectedTaskIndex = index;
            }

            clearMarkers();
            startTesting(index, false, 6);
        }

        void onAnalyze(CCObject*) {
            if (m_testing || !m_timeline)
                return;

            clearMarkers();
            generateAllTasks();
            if (s_tasks.empty()) {
                m_status->setString("No macro inputs to analyze.");
                return;
            }

            const int selectedEvent = m_timeline->getSelectedEventIndex();
            m_selectedTaskIndex = selectedEvent >= 0 && selectedEvent < static_cast<int>(s_tasks.size())
                ? selectedEvent
                : 0;

            // Analyze must start at the first input, not the selected one.
            // The selected task remains highlighted while the whole macro is scanned.
            startTesting(0, true, 24);
        }

        void onStop(CCObject*) {
            cancelTesting();
        }

        void onClear(CCObject*) {
            if (m_testing)
                cancelTesting();

            for (auto& task : s_tasks) {
                task.results.clear();
                task.alignments.clear();
                task.finished = false;
                task.tested = 0;
            }

            clearMarkers();
            refreshList();
            if (m_selectedTaskIndex >= 0 &&
                m_selectedTaskIndex < static_cast<int>(s_tasks.size())) {
                m_status->setString("Results cleared. Tasks kept.");
                updateDetails(s_tasks[m_selectedTaskIndex]);
            } else {
                m_status->setString("Results cleared. Select an input on the timeline.");
                m_detailsLabel->setString("Orb: player Y - orb Y     |     Portal: player X - portal X");
            }
        }

        void onSelectTask(CCObject* sender) {
            auto* item = static_cast<CCNode*>(sender);
            selectTask(item->getTag());
        }

        void generateAllTasks() {
            if (!m_timeline)
                return;

            const auto* events = m_timeline->getEvents();
            if (!events)
                return;

            s_tasks.clear();
            s_tasks.reserve(events->size());

            for (int i = 0; i < static_cast<int>(events->size()); ++i)
                s_tasks.push_back(makeTask(i));
        }

        void updateTaskRunner(float) {
            if (!m_testing) {
                if (m_timeline) {
                    const int selected = m_timeline->getSelectedEventIndex();
                    if (selected >= 0) {
                        int existing = -1;
                        for (int i = 0; i < static_cast<int>(s_tasks.size()); ++i) {
                            if (s_tasks[i].eventIndex == selected) {
                                existing = i;
                                break;
                            }
                        }

                        if (existing < 0) {
                            s_tasks.push_back(makeTask(selected));
                            existing = static_cast<int>(s_tasks.size()) - 1;
                            refreshList();
                        }

                        if (existing >= 0 && existing != m_selectedTaskIndex)
                            selectTask(existing);
                    }
                }
                return;
            }

            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return finishTesting();

            const int frame = Global::getCurrentFrame();
            PlayerObject* player =
                s_tasks[m_taskIndex].player2 ? pl->m_player2 : pl->m_player1;

            if (!player) {
                recordResult(false, true);
                return;
            }

            if ((pl->m_player1 && pl->m_player1->m_isDead) ||
                (pl->m_player2 && pl->m_player2->m_isDead)) {
                recordResult(false, true);
                return;
            }

            if (pl->m_levelEndAnimationStarted) {
                recordResult(true, false);
                return;
            }

            if (frame >= m_endFrame)
                recordResult(true, false);
        }

        static std::string formatTime(int frame, double subframe) {
            if (std::abs(subframe) < 0.0001)
                return std::to_string(frame);

            return fmt::format(
                "{}.{:02d}",
                frame,
                static_cast<int>(std::round(subframe * 100.0))
            );
        }

        void refreshList() {
            if (!m_list)
                return;

            m_list->removeAllChildren();

            if (m_countLabel) {
                m_countLabel->setString(
                    fmt::format(
                        "{} INPUT{}",
                        s_tasks.size(),
                        s_tasks.size() == 1 ? "" : "S"
                    ).c_str()
                );
            }

            constexpr float rowHeight = 38.0f;
            constexpr float rowWidth = 480.0f;
            float y = 196.0f;

            int start = 0;
            if (s_tasks.size() > 3 && m_selectedTaskIndex >= 3)
                start = m_selectedTaskIndex - 2;

            const int end = std::min<int>(
                static_cast<int>(s_tasks.size()),
                start + 3
            );

            for (int i = start; i < end; ++i) {
                const auto& task = s_tasks[i];
                const bool selected = i == m_selectedTaskIndex;

                auto* row = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
                row->setContentSize({rowWidth, rowHeight - 3.0f});
                row->setPosition({260, y});
                row->setColor(selected ? ccColor3B{38, 55, 72} : ccColor3B{23, 26, 32});
                row->setOpacity(selected ? 245 : 205);
                m_list->addChild(row, 0);

                auto* num = CCLabelBMFont::create(
                    fmt::format("{:02}", i + 1).c_str(),
                    "bigFont.fnt"
                );
                num->setScale(0.34f);
                num->setPosition({39, y});
                num->setOpacity(170);
                m_list->addChild(num, 2);

                auto* time = CCLabelBMFont::create(
                    formatTime(task.frame, task.subframe).c_str(),
                    "bigFont.fnt"
                );
                time->setScale(0.40f);
                time->setAnchorPoint({0, 0.5f});
                time->setPosition({58, y + 5});
                m_list->addChild(time, 2);

                auto* meta = CCLabelBMFont::create(
                    fmt::format(
                        "{} {} | {}",
                        task.player2 ? "P2" : "P1",
                        task.down ? "PRESS" : "RELEASE",
                        task.button == 1 ? "JUMP" :
                        task.button == 2 ? "LEFT" :
                        task.button == 3 ? "RIGHT" : "?"
                    ).c_str(),
                    "chatFont.fnt"
                );
                meta->setScale(0.26f);
                meta->setAnchorPoint({0, 0.5f});
                meta->setPosition({58, y - 8});
                meta->setOpacity(145);
                m_list->addChild(meta, 2);

                std::string stateText = "READY";
                if (m_testing && i == m_taskIndex)
                    stateText = fmt::format("{} TESTED", task.tested);
                else if (task.finished)
                    stateText = formatTaskSummary(task);
                else if (!task.results.empty())
                    stateText = fmt::format("{} SAMPLES", task.tested);

                auto* state = CCLabelBMFont::create(stateText.c_str(), "chatFont.fnt");
                state->setScale(0.29f);
                state->setAnchorPoint({1, 0.5f});
                state->setPosition({493, y});
                state->setOpacity(task.finished ? 225 : 150);
                m_list->addChild(state, 2);

                auto* item = CCMenuItemLabel::create(
                    CCLabelBMFont::create("", "chatFont.fnt"),
                    this,
                    menu_selector(FrameTaskPopup::onSelectTask)
                );
                item->setContentSize({rowWidth, rowHeight});
                item->setTag(i);
                item->setPosition({260, y});
                m_list->addChild(item, 5);

                y -= rowHeight;
            }

            if (s_tasks.empty()) {
                auto* empty = CCLabelBMFont::create(
                    "Select an input, then press TASK or ANALYZE",
                    "chatFont.fnt"
                );
                empty->setScale(0.31f);
                empty->setPosition({260, 150});
                empty->setOpacity(150);
                m_list->addChild(empty, 2);
            }
        }

        static std::string formatTaskSummary(const FrameTask& task) {
            int orbCount = 0;
            int portalCount = 0;
            for (const auto& sample : task.alignments) {
                if (sample.kind == AlignmentKind::Orb)
                    ++orbCount;
                else
                    ++portalCount;
            }

            if (orbCount == 0 && portalCount == 0)
                return "NO ALIGNMENT";

            if (orbCount > 0 && portalCount > 0)
                return fmt::format("O:{}Y P:{}X", orbCount, portalCount);
            if (orbCount > 0)
                return fmt::format("ORB {}Y", orbCount);
            return fmt::format("PORTAL {}X", portalCount);
        }

        static std::string formatSample(const AlignmentSample& sample) {
            const char* type = sample.kind == AlignmentKind::Orb ? "ORB" : "PORTAL";
            const char axis = sample.kind == AlignmentKind::Orb ? 'Y' : 'X';
            return fmt::format(
                "{} #{} {}{:+.1f} @{}",
                type,
                sample.objectId,
                axis,
                sample.axisDelta,
                formatTime(sample.inputFrame, sample.inputSubframe)
            );
        }

        void updateDetails(const FrameTask& task) {
            if (!m_detailsLabel)
                return;

            if (task.alignments.empty()) {
                m_detailsLabel->setString(
                    task.finished
                        ? "No orb or portal callback was observed in this scan."
                        : "Orb: player Y - orb Y     |     Portal: player X - portal X"
                );
                return;
            }

            std::string details;
            int shown = 0;
            for (const auto& sample : task.alignments) {
                if (!details.empty())
                    details += "  |  ";
                details += formatSample(sample);
                if (++shown >= 2)
                    break;
            }

            if (task.alignments.size() > 2)
                details += fmt::format("  |  +{} more", task.alignments.size() - 2);

            m_detailsLabel->setString(details.c_str());
        }

        static int findTaskEvent(const Macro& source, const FrameTask& task) {
            const double target = static_cast<double>(task.frame) + task.subframe;

            // Preserve exact identity when the macro contains duplicate actions
            // at the same timestamp. Falling back to nearest matching timing keeps
            // the lookup resilient if a timeline edit has reordered its inputs.
            if (task.eventIndex >= 0 &&
                task.eventIndex < static_cast<int>(source.inputs.size())) {
                const auto& event = source.inputs[task.eventIndex];
                if (event.button == task.button &&
                    event.player2 == task.player2 &&
                    event.down == task.down &&
                    std::abs(event.getPreciseFrame() - target) < 0.0001)
                    return task.eventIndex;
            }

            int best = -1;
            double bestDistance = std::numeric_limits<double>::max();

            for (int i = 0; i < static_cast<int>(source.inputs.size()); ++i) {
                const auto& event = source.inputs[i];
                if (event.button != task.button ||
                    event.player2 != task.player2 ||
                    event.down != task.down)
                    continue;

                const double distance = std::abs(event.getPreciseFrame() - target);
                if (distance < 0.0001)
                    return i;

                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = i;
                }
            }

            return best;
        }

        void buildCandidates(const FrameTask& task) {
            m_candidates.clear();
            m_candidates.push_back({task.frame, task.subframe});

            for (int offset = 1; offset <= m_searchRadius; ++offset) {
                if (task.frame - offset >= 0)
                    m_candidates.push_back({task.frame - offset, task.subframe});
                m_candidates.push_back({task.frame + offset, task.subframe});
            }
        }

        void startTesting(int taskIndex, bool all, int radius) {
            if (m_testing ||
                taskIndex < 0 ||
                taskIndex >= static_cast<int>(s_tasks.size()))
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
            m_backupRespawnFrame = Global::get().respawnFrame;

            m_taskIndex = taskIndex;
            m_testingAll = all;
            m_completedAll = false;
            m_searchRadius = radius;
            m_testing = true;

            if (m_testingAll) {
                for (auto& task : s_tasks) {
                    task.results.clear();
                    task.alignments.clear();
                    task.finished = false;
                    task.tested = 0;
                }
            } else {
                auto& task = s_tasks[m_taskIndex];
                task.results.clear();
                task.alignments.clear();
                task.finished = false;
                task.tested = 0;
            }

            buildCandidates(s_tasks[m_taskIndex]);

            auto& g = Global::get();
            g.macro = m_backupMacro;
            g.state = state::playing;
            g.currentAction = 0;
            g.currentFrameFix = 0;
            g.restart = true;
            g.firstAttempt = true;
            g.respawnFrame = -1;

            m_candidateIndex = 0;
            beginCandidate();
        }

        void beginCandidate() {
            if (!m_testing)
                return;

            if (m_taskIndex < 0 || m_taskIndex >= static_cast<int>(s_tasks.size()))
                return finishTesting();

            if (m_candidateIndex >= static_cast<int>(m_candidates.size())) {
                auto& task = s_tasks[m_taskIndex];
                task.finished = true;

                for (const auto& sample : task.alignments)
                    addMarker(sample);

                if (m_testingAll && m_taskIndex + 1 < static_cast<int>(s_tasks.size())) {
                    ++m_taskIndex;
                    buildCandidates(s_tasks[m_taskIndex]);
                    m_candidateIndex = 0;
                    beginCandidate();
                    return;
                }

                m_completedAll = m_testingAll;
                return finishTesting();
            }

            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return finishTesting();

            auto& task = s_tasks[m_taskIndex];
            const Candidate candidate = m_candidates[m_candidateIndex];

            Macro candidateMacro = m_backupMacro;
            const int eventIndex = findTaskEvent(candidateMacro, task);
            if (eventIndex < 0) {
                ++m_candidateIndex;
                beginCandidate();
                return;
            }

            candidateMacro.inputs[eventIndex].setPreciseFrame(
                static_cast<double>(candidate.frame) + candidate.subframe
            );
            std::stable_sort(candidateMacro.inputs.begin(), candidateMacro.inputs.end());

            const double candidateTime =
                static_cast<double>(candidate.frame) + candidate.subframe;
            double endTime = candidateTime + 48.0;

            for (const auto& event : candidateMacro.inputs) {
                if (event.player2 == task.player2 &&
                    event.button <= 3 &&
                    event.getPreciseFrame() > candidateTime + 0.0001) {
                    endTime = std::min(endTime, event.getPreciseFrame());
                    break;
                }
            }

            m_endFrame = std::max(
                candidate.frame + 1,
                static_cast<int>(std::ceil(endTime))
            );

            auto& g = Global::get();
            g.macro = std::move(candidateMacro);
            g.state = state::playing;
            g.currentAction = 0;
            g.currentFrameFix = 0;
            g.restart = true;
            g.firstAttempt = true;
            g.respawnFrame = -1;

            g_alignmentProbe.reset();
            g_alignmentProbe.active = true;
            g_alignmentProbe.player2 = task.player2;
            g_alignmentProbe.firstFrame = std::max(0, candidate.frame - 1);
            g_alignmentProbe.lastFrame = m_endFrame + 1;
            g_alignmentProbe.inputFrame = candidate.frame;
            g_alignmentProbe.inputSubframe = candidate.subframe;

            m_targetFrame = candidate.frame;
            m_targetSubframe = candidate.subframe;
            m_attemptPosition = {0, 0};

            // Every timing trial must start with a clean CBF queue, otherwise
            // sub-frame inputs armed by the previous trial can leak into this one.
            cbf::Engine::get()->reset();
            pl->resetLevelFromStart();

            if (m_status) {
                m_status->setString(
                    fmt::format(
                        "{} input {}/{} | timing {}",
                        m_testingAll ? "Analyze" : "Task",
                        m_taskIndex + 1,
                        s_tasks.size(),
                        formatTime(m_targetFrame, m_targetSubframe)
                    ).c_str()
                );
            }
            refreshList();
        }

        void recordResult(bool survived, bool died) {
            if (!m_testing)
                return;

            g_alignmentProbe.active = false;
            auto& task = s_tasks[m_taskIndex];

            FrameTaskResult result;
            result.frame = m_targetFrame;
            result.subframe = m_targetSubframe;
            result.survived = survived;
            result.died = died;
            result.alignments = g_alignmentProbe.hits;

            if (PlayLayer* pl = PlayLayer::get()) {
                PlayerObject* player = task.player2 ? pl->m_player2 : pl->m_player1;
                if (player)
                    result.position = player->getPosition();
            }

            task.results.push_back(result);
            ++task.tested;

            for (const auto& sample : result.alignments) {
                const auto duplicate = std::find_if(
                    task.alignments.begin(),
                    task.alignments.end(),
                    [&](const AlignmentSample& existing) {
                        return existing.kind == sample.kind &&
                            existing.objectId == sample.objectId &&
                            std::abs(existing.objectPosition.x - sample.objectPosition.x) < 0.1f &&
                            std::abs(existing.objectPosition.y - sample.objectPosition.y) < 0.1f &&
                            std::abs(existing.axisDelta - sample.axisDelta) < 0.5f;
                    }
                );

                if (duplicate == task.alignments.end())
                    task.alignments.push_back(sample);
            }

            ++m_candidateIndex;
            beginCandidate();
        }

        void addMarker(const AlignmentSample& sample) {
            PlayLayer* pl = PlayLayer::get();
            if (!pl)
                return;

            auto* markerLayer = pl->getChildByTag(markerTag);
            if (!markerLayer) {
                markerLayer = CCLayer::create();
                markerLayer->setTag(markerTag);
                markerLayer->setZOrder(100000);
                pl->addChild(markerLayer);
            }

            const bool orb = sample.kind == AlignmentKind::Orb;
            const auto fill = orb
                ? ccc4f(1.0f, 0.72f, 0.15f, 0.22f)
                : ccc4f(0.15f, 0.78f, 1.0f, 0.22f);
            const auto line = orb
                ? ccc4f(1.0f, 0.72f, 0.15f, 0.95f)
                : ccc4f(0.15f, 0.78f, 1.0f, 0.95f);

            auto* draw = CCDrawNode::create();
            draw->drawCircle(sample.playerPosition, 8.0f, fill, 1.5f, line, 24);
            markerLayer->addChild(draw);

            auto* label = CCLabelBMFont::create(
                fmt::format(
                    "{} {}{:+.1f}",
                    orb ? "ORB" : "PORTAL",
                    orb ? "Y" : "X",
                    sample.axisDelta
                ).c_str(),
                "chatFont.fnt"
            );
            label->setScale(0.31f);
            label->setAnchorPoint({0.5f, 0.0f});
            label->setPosition({
                sample.playerPosition.x,
                sample.playerPosition.y + 9.0f
            });
            markerLayer->addChild(label);
        }

        void clearMarkers() {
            if (auto* pl = PlayLayer::get()) {
                if (auto* markerLayer = pl->getChildByTag(markerTag))
                    markerLayer->removeFromParentAndCleanup(true);
            }
        }

        void finishTesting() {
            if (!m_testing)
                return;

            auto& g = Global::get();
            g_alignmentProbe.reset();
            g.macro = m_backupMacro;
            g.state = m_backupState;
            g.currentAction = m_backupCurrentAction;
            g.currentFrameFix = m_backupCurrentFrameFix;
            g.restart = m_backupRestart;
            g.firstAttempt = m_backupFirstAttempt;
            g.respawnFrame = m_backupRespawnFrame;
            cbf::Engine::get()->reset();
            cbf::setDividerOverride(0);

            m_testing = false;
            if (m_completedAll) {
                for (auto& task : s_tasks)
                    task.finished = true;
            } else if (m_taskIndex >= 0 && m_taskIndex < static_cast<int>(s_tasks.size())) {
                s_tasks[m_taskIndex].finished = true;
                m_selectedTaskIndex = m_taskIndex;
            }

            Macro::updateTPS();
            refreshList();

            if (m_completedAll) {
                int orbAlignments = 0;
                int portalAlignments = 0;
                int tested = 0;
                for (const auto& task : s_tasks) {
                    tested += task.tested;
                    for (const auto& sample : task.alignments) {
                        if (sample.kind == AlignmentKind::Orb)
                            ++orbAlignments;
                        else
                            ++portalAlignments;
                    }
                }

                m_status->setString(
                    fmt::format(
                        "Analyze complete: {} timings, {} orb Y and {} portal X alignments.",
                        tested,
                        orbAlignments,
                        portalAlignments
                    ).c_str()
                );
                if (m_selectedTaskIndex >= 0 && m_selectedTaskIndex < static_cast<int>(s_tasks.size()))
                    updateDetails(s_tasks[m_selectedTaskIndex]);
            } else if (m_taskIndex >= 0 && m_taskIndex < static_cast<int>(s_tasks.size())) {
                const auto& task = s_tasks[m_taskIndex];
                m_status->setString(
                    fmt::format(
                        "Task complete: {} timings checked, {} alignments found.",
                        task.tested,
                        task.alignments.size()
                    ).c_str()
                );
                updateDetails(task);
            }
        }

        void cancelTesting() {
            if (!m_testing) {
                g_alignmentProbe.reset();
                cbf::setDividerOverride(0);
                return;
            }

            auto& g = Global::get();
            g_alignmentProbe.reset();
            g.macro = m_backupMacro;
            g.state = m_backupState;
            g.currentAction = m_backupCurrentAction;
            g.currentFrameFix = m_backupCurrentFrameFix;
            g.restart = m_backupRestart;
            g.firstAttempt = m_backupFirstAttempt;
            g.respawnFrame = m_backupRespawnFrame;
            cbf::Engine::get()->reset();
            cbf::setDividerOverride(0);

            m_testing = false;
            m_status->setString("Scan stopped. Original macro state restored.");
            refreshList();
            Macro::updateTPS();
        }

    private:
        struct Candidate {
            int frame = 0;
            double subframe = 0.0;
        };

        MacroTimeline* m_timeline = nullptr;
        CCMenu* m_actionMenu = nullptr;
        CCMenu* m_list = nullptr;
        CCLabelBMFont* m_status = nullptr;
        CCLabelBMFont* m_headerLabel = nullptr;
        CCLabelBMFont* m_countLabel = nullptr;
        CCLabelBMFont* m_selectedLabel = nullptr;
        CCLabelBMFont* m_detailsLabel = nullptr;

        bool m_testing = false;
        bool m_testingAll = false;
        bool m_completedAll = false;
        int m_selectedTaskIndex = -1;
        int m_taskIndex = -1;
        int m_candidateIndex = 0;
        int m_searchRadius = 6;
        int m_targetFrame = 0;
        double m_targetSubframe = 0.0;
        int m_endFrame = 0;
        cocos2d::CCPoint m_attemptPosition = {0, 0};

        Macro m_backupMacro;
        state m_backupState = state::none;
        size_t m_backupCurrentAction = 0;
        size_t m_backupCurrentFrameFix = 0;
        bool m_backupRestart = false;
        bool m_backupFirstAttempt = false;
        int m_backupRespawnFrame = -1;

        std::vector<Candidate> m_candidates;
        static inline std::vector<FrameTask> s_tasks;
    };
}

MacroTimelineLayer* MacroTimelineLayer::create(Macro* macro) {
    auto* ret = new MacroTimelineLayer();
    ret->macro = macro;
    ret->timeline = std::make_unique<MacroTimeline>(macro);
    ret->inspector = std::make_unique<MacroEventInspector>(ret->timeline.get());

    auto win = CCDirector::sharedDirector()->getWinSize();
    float width = win.width - 24.0f;

    if (ret->initAnchored(width, 235.0f, macro, Utils::getTexture().c_str())) {
        // This is an in-game overlay, not a conventional popup.
        ret->m_bgSprite->setVisible(false);
        if (ret->m_closeBtn)
            ret->m_closeBtn->setVisible(false);
        if (ret->m_title)
            ret->m_title->setVisible(false);

        ret->autorelease();
        return ret;
    }

    delete ret;
    return nullptr;
}

void MacroTimelineLayer::restorePauseMenu() {
    if (!pauseMenuWasVisible || !hiddenPauseLayer)
        return;

    auto* pl = PlayLayer::get();

    if (gameWasPaused && pl && !pl->m_isPaused)
        pl->pauseGame(false);

    if (hiddenPauseLayer)
        hiddenPauseLayer->setVisible(true);

    pauseMenuWasVisible = false;
}

MacroTimelineLayer::~MacroTimelineLayer() {
    restorePauseMenu();
}

bool MacroTimelineLayer::setup(Macro* setupMacro) {
    macro = setupMacro;

    this->setTitle("Macro Timeline");

    // The timeline is opened from the pause menu, but it is an in-game overlay.
    // Temporarily hide the pause layer and resume the level so the player remains controllable.
    if (auto* pause = Global::getPauseLayer()) {
        hiddenPauseLayer = pause;
        pauseMenuWasVisible = pause->isVisible();

        if (auto* pl = PlayLayer::get())
            gameWasPaused = pl->m_isPaused;

        if (pauseMenuWasVisible)
            pause->setVisible(false);

        if (gameWasPaused) {
            if (auto* pl = PlayLayer::get())
                pl->pauseGame(true);
        }
    }

    // A popup's m_mainLayer has its origin at the popup's bottom-left, not the screen
    // centre, and this overlay is wider than the popup it was created from. Lay everything
    // out under our own root, centred horizontally in screen space and sitting just above
    // the bottom edge (content spans roughly -92 .. +27 around the root).
    {
        auto win = CCDirector::sharedDirector()->getWinSize();
        overlay = CCNode::create();
        overlay->setPosition({win.width / 2.0f, 92.0f + 8.0f});
        this->addChild(overlay, 10);
    }

    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    this->registerWithTouchDispatcher();

    // Set up main content
    initToolbar();
    initTimeline();
    initInspector();
    inspectorBg->setVisible(false);
    inspectorMenu->setVisible(false);
    for (auto* label : inspectorLabels)
        if (label) label->setVisible(false);

    bool const cbfEnabled = Mod::get()->getSavedValue<bool>("macro_cbf", true);
    timeline->setPlayhead(0, 0.0);
    if (cbfModeToggle)
        cbfModeToggle->setOpacity(cbfEnabled ? 255 : 140);

    scheduleUpdate();
    return true;
}

void MacroTimelineLayer::keyBackClicked() {
    restorePauseMenu();
    this->onClose(nullptr);
}

void MacroTimelineLayer::onTasksPressed(CCObject*) {
    if (!timeline)
        return;

    auto* timelinePtr = timeline.get();
    Loader::get()->queueInMainThread([timelinePtr] {
        if (auto* popup = FrameTaskPopup::create(timelinePtr))
            popup->show();
    }); 
}

void MacroTimelineLayer::initInspector() {
    constexpr float width = 180.0f;
    constexpr float height = 264.0f;

    inspectorBg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    inspectorBg->setContentSize({width, height});
    inspectorBg->setAnchorPoint({1.0f, 0.0f});
    inspectorBg->setPosition({CCDirector::sharedDirector()->getWinSize().width / 2.0f - 12.0f, 8.0f});
    inspectorBg->setColor({30, 30, 36});
    inspectorBg->setOpacity(235);
    inspectorBg->setZOrder(25);
    overlay->addChild(inspectorBg);

    auto* title = CCLabelBMFont::create("Event Inspector", "bigFont.fnt");
    title->setScale(0.40f);
    title->setPosition({width / 2.0f, height - 20.0f});
    inspectorBg->addChild(title);

    inspectorMenu = CCMenu::create();
    inspectorMenu->setPosition({0.0f, 0.0f});
    inspectorBg->addChild(inspectorMenu);

    auto addButton = [&](const char* text, float x, float y, SEL_MenuHandler cb) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(0.34f);
        auto* item = CCMenuItemLabel::create(label, this, cb);
        item->setPosition({x, y});
        inspectorMenu->addChild(item);
    };

    addButton("-", 22.0f, 214.0f, menu_selector(MacroTimelineLayer::onFrameDown));
    addButton("+", 158.0f, 214.0f, menu_selector(MacroTimelineLayer::onFrameUp));
    addButton("-", 22.0f, 185.0f, menu_selector(MacroTimelineLayer::onSubframeDown));
    addButton("+", 158.0f, 185.0f, menu_selector(MacroTimelineLayer::onSubframeUp));
    addButton("Button", 90.0f, 151.0f, menu_selector(MacroTimelineLayer::onButtonCycle));
    addButton("Player", 47.0f, 125.0f, menu_selector(MacroTimelineLayer::onPlayerToggle));
    addButton("Action", 133.0f, 125.0f, menu_selector(MacroTimelineLayer::onActionToggle));

    for (int i = 0; i < 8; ++i) {
        inspectorLabels[i] = CCLabelBMFont::create("", "chatFont.fnt");
        inspectorLabels[i]->setScale(0.32f);
        inspectorLabels[i]->setAnchorPoint({0.0f, 0.5f});
        inspectorLabels[i]->setPosition({10.0f, 108.0f - i * 13.0f});
        inspectorBg->addChild(inspectorLabels[i]);
    }
}

void MacroTimelineLayer::initToolbar() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    float width = win.width - 24.0f;

    auto* toolbarBg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    toolbarBg->setContentSize({width, 38.0f});
    toolbarBg->setPosition({0.0f, 204.0f});
    toolbarBg->setColor({18, 20, 26});
    toolbarBg->setOpacity(232);
    toolbarBg->setZOrder(90);
    overlay->addChild(toolbarBg);

    toolbarMenu = CCMenu::create();
    toolbarMenu->setPosition({0.0f, 204.0f});
    toolbarMenu->setZOrder(100);
    overlay->addChild(toolbarMenu);

    struct Entry {
        char const* text;
        SEL_MenuHandler cb;
        CCMenuItemSpriteExtra** out;
        ButtonSprite* sprite = nullptr;
    };

    CCMenuItemSpriteExtra* undoBtn = nullptr;
    CCMenuItemSpriteExtra* redoBtn = nullptr;
    CCMenuItemSpriteExtra* taskBtn = nullptr;

    std::vector<Entry> entries = {
        { "Play",   menu_selector(MacroTimelineLayer::onPlayPressed),    &playBtn },
        { "Pause",  menu_selector(MacroTimelineLayer::onPausePressed),   &pauseBtn },
        { "Stop",   menu_selector(MacroTimelineLayer::onStopPressed),    &stopBtn },
        { "Step",   menu_selector(MacroTimelineLayer::onStepFramePressed), &stepFrameBtn },
        { "CBF",    menu_selector(MacroTimelineLayer::onCBFTogglePressed), &cbfModeToggle },
        { "Zoom -", menu_selector(MacroTimelineLayer::onZoomOutPressed), &zoomOutBtn },
        { "Zoom +", menu_selector(MacroTimelineLayer::onZoomInPressed),  &zoomInBtn },
        { "Undo",   menu_selector(MacroTimelineLayer::onUndoPressed),    &undoBtn },
        { "Redo",   menu_selector(MacroTimelineLayer::onRedoPressed),    &redoBtn },
        { "Tasks",  menu_selector(MacroTimelineLayer::onTasksPressed),   &taskBtn },
    };

    // Measure at full size, then pick one scale so the whole row fits the overlay.
    constexpr float maxScale = 0.50f;
    constexpr float gap = 6.0f;
    constexpr float margin = 12.0f;

    float natural = 0.0f;
    for (auto& e : entries) {
        e.sprite = ButtonSprite::create(e.text);
        natural += e.sprite->getContentSize().width;
    }

    float available = width - margin * 2.0f - gap * (entries.size() - 1);
    float scale = std::min(maxScale, available / natural);

    float total = natural * scale + gap * (entries.size() - 1);
    float x = -total / 2.0f;

    for (auto& e : entries) {
        e.sprite->setScale(scale);
        auto* item = CCMenuItemSpriteExtra::create(e.sprite, this, e.cb);
        float w = e.sprite->getContentSize().width * scale;
        item->setPositionX(x + w / 2.0f);
        x += w + gap;
        toolbarMenu->addChild(item);
        *e.out = item;
    }

    taskBtn->setID("tasks");
}

void MacroTimelineLayer::initTimeline() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    constexpr float bottom = 42.0f;
    float width = win.width - 24.0f;

    renderState.timelineSize = CCSizeMake(width, 132.0f);
    renderState.rulerHeight = 28.0f;
    renderState.trackHeight = 42.0f;
    renderState.eventHeight = 20.0f;

    timelineLayer = CCLayer::create();
    timelineLayer->setPosition({-width / 2.0f, bottom});
    timelineLayer->setContentSize(renderState.timelineSize);
    timelineLayer->setZOrder(20);
    overlay->addChild(timelineLayer);

    auto* bg = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    bg->setContentSize(renderState.timelineSize);
    bg->setPosition(renderState.timelineSize / 2.0f);
    bg->setColor({10, 12, 17});
    bg->setOpacity(242);
    timelineLayer->addChild(bg);

    auto* topBar = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    topBar->setContentSize({width - 2.0f, renderState.rulerHeight});
    topBar->setPosition({
        width / 2.0f,
        renderState.timelineSize.height - renderState.rulerHeight / 2.0f
    });
    topBar->setColor({23, 26, 34});
    topBar->setOpacity(245);
    timelineLayer->addChild(topBar, 1);

    auto* footer = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    footer->setContentSize({width - 2.0f, 20.0f});
    footer->setPosition({width / 2.0f, 10.0f});
    footer->setColor({21, 24, 31});
    footer->setOpacity(245);
    timelineLayer->addChild(footer, 1);

    rulerLayer = CCLayer::create();
    rulerLayer->setContentSize({width, renderState.rulerHeight});
    rulerLayer->setPosition({
        0.0f,
        renderState.timelineSize.height - renderState.rulerHeight
    });
    timelineLayer->addChild(rulerLayer, 3);

    eventsLayer = CCLayer::create();
    eventsLayer->setContentSize({
        width,
        renderState.timelineSize.height - renderState.rulerHeight - 20.0f
    });
    eventsLayer->setPosition({0.0f, 20.0f});
    timelineLayer->addChild(eventsLayer, 2);

    cursorLayer = CCLayer::create();
    cursorLayer->setContentSize({
        width,
        renderState.timelineSize.height - 20.0f
    });
    cursorLayer->setPosition({0.0f, 20.0f});
    cursorLayer->setZOrder(10);
    timelineLayer->addChild(cursorLayer);

    frameCounterLabel = CCLabelBMFont::create("FRAME 0000", "chatFont.fnt");
    frameCounterLabel->setScale(0.46f);
    frameCounterLabel->setAnchorPoint({0.0f, 0.5f});
    frameCounterLabel->setPosition({14.0f, 10.0f});
    timelineLayer->addChild(frameCounterLabel, 5);

    subframeLabel = CCLabelBMFont::create("SUB 00%", "chatFont.fnt");
    subframeLabel->setScale(0.46f);
    subframeLabel->setAnchorPoint({0.0f, 0.5f});
    subframeLabel->setPosition({104.0f, 10.0f});
    timelineLayer->addChild(subframeLabel, 5);

    timelineInfoLabel = CCLabelBMFont::create("", "chatFont.fnt");
    timelineInfoLabel->setScale(0.31f);
    timelineInfoLabel->setAnchorPoint({1.0f, 0.5f});
    timelineInfoLabel->setOpacity(180);
    timelineInfoLabel->setPosition({width - 14.0f, 10.0f});
    timelineLayer->addChild(timelineInfoLabel, 5);
}

void MacroTimelineLayer::updateTimeline(float dt) {
    if (!timeline) return;

    if (Global::get().state == state::playing) {
        int frame = Global::getCurrentFrame();
        timeline->setPlayhead(frame, 0.0);
    }

    const float ppf = std::max(0.5f, renderState.pixelsPerFrame);
    const float scroll = static_cast<float>(timeline->getScrollOffset());
    renderState.firstVisibleFrame =
        std::max(0, static_cast<int>(std::floor(scroll / ppf)) - 2);
    renderState.lastVisibleFrame =
        std::max(
            renderState.firstVisibleFrame + 1,
            static_cast<int>(std::ceil(
                (scroll + renderState.timelineSize.width) / ppf
            )) + 2
        );

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

    const float trackHeight = renderState.trackHeight;
    const float p1Y = trackHeight * 1.5f;
    const float p2Y = trackHeight * 0.5f;

    auto drawLine = [&](float y, float alpha) {
        auto* line = CCDrawNode::create();
        line->drawRect(
            CCRectMake(0.0f, y, renderState.timelineSize.width, 1.0f),
            ccc4f(1.0f, 1.0f, 1.0f, alpha),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );
        eventsLayer->addChild(line);
    };

    drawLine(0.0f, 0.12f);
    drawLine(trackHeight, 0.38f);
    drawLine(trackHeight * 2.0f, 0.12f);

    auto* lanes = CCDrawNode::create();
    lanes->drawRect(
        CCRectMake(
            0.0f,
            trackHeight,
            renderState.timelineSize.width,
            trackHeight
        ),
        ccc4f(0.18f, 0.22f, 0.30f, 0.10f),
        0.0f,
        ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
    );
    eventsLayer->addChild(lanes, -3);

    const float ppf = std::max(0.5f, renderState.pixelsPerFrame);
    const int gridStep =
        ppf >= 10.0f ? 5 :
        ppf >= 6.0f ? 10 :
        ppf >= 3.0f ? 20 : 30;

    auto* grid = CCDrawNode::create();
    const int firstGrid =
        (renderState.firstVisibleFrame / gridStep) * gridStep;

    for (int frame = firstGrid;
         frame <= renderState.lastVisibleFrame;
         frame += gridStep) {
        const float x = frameToPixels(frame);
        if (x < -1.0f || x > renderState.timelineSize.width + 1.0f)
            continue;

        grid->drawRect(
            CCRectMake(
                x - 0.5f,
                0.0f,
                1.0f,
                trackHeight * 2.0f
            ),
            ccc4f(0.42f, 0.47f, 0.58f, 0.18f),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );
    }
    eventsLayer->addChild(grid, -2);

    auto* p1 = CCLabelBMFont::create("P1", "chatFont.fnt");
    p1->setScale(0.30f);
    p1->setAnchorPoint({0.0f, 0.5f});
    p1->setPosition({8.0f, p1Y});
    p1->setOpacity(160);
    eventsLayer->addChild(p1, 2);

    auto* p2 = CCLabelBMFont::create("P2", "chatFont.fnt");
    p2->setScale(0.30f);
    p2->setAnchorPoint({0.0f, 0.5f});
    p2->setPosition({8.0f, p2Y});
    p2->setOpacity(160);
    eventsLayer->addChild(p2, 2);

    if (!macro || macro->inputs.empty())
        return;

    const float labelStartX = 30.0f;

    for (int i = 0; i < static_cast<int>(macro->inputs.size()); ++i) {
        const auto& evt = macro->inputs[i];
        const float x = frameToPixels(evt.frame, evt.subframe);

        if (x < -20.0f || x > renderState.timelineSize.width + 20.0f)
            continue;

        const float y = evt.player2 ? p2Y : p1Y;
        const ccColor3B color = getActionColor(evt.down);
        const bool selected = timeline->isEventSelected(i);

        auto* marker = CCDrawNode::create();

        marker->drawRect(
            CCRectMake(x - 0.75f, y - 11.0f, 1.5f, 22.0f),
            ccc4f(
                static_cast<float>(color.r) / 255.0f,
                static_cast<float>(color.g) / 255.0f,
                static_cast<float>(color.b) / 255.0f,
                selected ? 0.72f : 0.28f
            ),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );

        marker->drawCircle(
            {x, y},
            selected ? 7.0f : 5.0f,
            ccc4f(
                static_cast<float>(color.r) / 255.0f,
                static_cast<float>(color.g) / 255.0f,
                static_cast<float>(color.b) / 255.0f,
                selected ? 1.0f : 0.86f
            ),
            selected ? 2.0f : 0.0f,
            ccc4f(1.0f, 1.0f, 1.0f, selected ? 0.82f : 0.0f),
            16
        );

        if (!evt.down) {
            marker->drawCircle(
                {x, y},
                3.0f,
                ccc4f(10.0f / 255.0f, 12.0f / 255.0f, 17.0f / 255.0f, 0.95f),
                1.0f,
                ccc4f(
                    static_cast<float>(color.r) / 255.0f,
                    static_cast<float>(color.g) / 255.0f,
                    static_cast<float>(color.b) / 255.0f,
                    0.95f
                ),
                16
            );
        }

        marker->setZOrder(selected ? 20 : 10);
        eventsLayer->addChild(marker);

        if (selected) {
            auto* time = CCLabelBMFont::create(
                fmt::format("{}.{}", evt.frame, static_cast<int>(evt.subframe * 1000.0)).c_str(),
                "chatFont.fnt"
            );
            time->setScale(0.29f);
            time->setAnchorPoint({0.0f, 0.5f});
            time->setPosition({
                std::max(labelStartX, x + 10.0f),
                y + 12.0f
            });
            time->setOpacity(235);
            time->setZOrder(25);
            eventsLayer->addChild(time);

            auto* action = CCLabelBMFont::create(
                fmt::format(
                    "{} {}",
                    evt.down ? "PRESS" : "RELEASE",
                    evt.button == 1 ? "JUMP" :
                    evt.button == 2 ? "LEFT" :
                    evt.button == 3 ? "RIGHT" : "?"
                ).c_str(),
                "chatFont.fnt"
            );
            action->setScale(0.24f);
            action->setAnchorPoint({0.0f, 0.5f});
            action->setPosition({
                std::max(labelStartX, x + 10.0f),
                y - 12.0f
            });
            action->setOpacity(165);
            action->setZOrder(25);
            eventsLayer->addChild(action);
        }
    }
}

void MacroTimelineLayer::renderRuler() {
    rulerLayer->removeAllChildren();

    const float ppf = std::max(0.5f, renderState.pixelsPerFrame);

    const int frameStep =
        ppf >= 10.0f ? 5 :
        ppf >= 6.0f ? 10 :
        ppf >= 3.0f ? 20 :
        ppf >= 1.5f ? 30 : 60;

    const int minorStep = std::max(1, frameStep / 5);
    const int first =
        (renderState.firstVisibleFrame / minorStep) * minorStep;

    auto* ticks = CCDrawNode::create();

    for (int frame = first;
         frame <= renderState.lastVisibleFrame;
         frame += minorStep) {
        const float x = frameToPixels(frame);
        if (x < -2.0f || x > renderState.timelineSize.width + 2.0f)
            continue;

        const bool major = frame % frameStep == 0;

        ticks->drawRect(
            CCRectMake(
                x - (major ? 0.75f : 0.5f),
                major ? 0.0f : 10.0f,
                major ? 1.5f : 1.0f,
                major ? 24.0f : 10.0f
            ),
            ccc4f(
                major ? 0.74f : 0.38f,
                major ? 0.77f : 0.41f,
                major ? 0.84f : 0.45f,
                major ? 0.78f : 0.32f
            ),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );

        if (major) {
            auto* label = CCLabelBMFont::create(
                std::to_string(frame).c_str(),
                "chatFont.fnt"
            );
            label->setScale(0.29f);
            label->setAnchorPoint({0.5f, 0.5f});
            label->setPosition({x, 20.0f});
            label->setOpacity(215);
            rulerLayer->addChild(label);
        }
    }

    rulerLayer->addChild(ticks, 0);

    auto* baseline = CCDrawNode::create();
    baseline->drawRect(
        CCRectMake(0.0f, 0.0f, renderState.timelineSize.width, 1.0f),
        ccc4f(0.45f, 0.52f, 0.64f, 0.42f),
        0.0f,
        ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
    );
    rulerLayer->addChild(baseline, 20);
}

void MacroTimelineLayer::renderPlayhead() {
    cursorLayer->removeAllChildren();

    float x = frameToPixels(timeline->getPlayheadFrame(), timeline->getPlayheadSubframe());

    if (x >= 0.0f && x <= renderState.timelineSize.width) {
        auto* cursor = CCDrawNode::create();
        cursor->drawRect(
            CCRectMake(
                x - 0.75f,
                0.0f,
                1.5f,
                renderState.timelineSize.height
            ),
            ccc4f(1.0f, 0.35f, 0.35f, 0.8f),
            0.0f,
            ccc4f(0.0f, 0.0f, 0.0f, 0.0f)
        );
        cursor->setZOrder(100);
        cursorLayer->addChild(cursor);
    }
}

void MacroTimelineLayer::renderInspector() {
    if (!inspectorBg || !inspectorBg->isVisible())
        return;

    if (!inspector->hasData()) {
        for (auto* label : inspectorLabels)
            if (label) label->setVisible(false);
        return;
    }

    for (auto* label : inspectorLabels)
        if (label) label->setVisible(true);
}

void MacroTimelineLayer::updateFrameCounter() {
    const int frame = timeline->getPlayheadFrame();
    const int percent =
        std::clamp(static_cast<int>(timeline->getPlayheadSubframe() * 100.0), 0, 99);

    frameCounterLabel->setString(
        fmt::format("FRAME {:04}", std::max(0, frame)).c_str()
    );

    subframeLabel->setString(
        fmt::format("SUB {:02}%", percent).c_str()
    );

    if (timelineInfoLabel) {
        timelineInfoLabel->setString(
            fmt::format(
                "{} INPUTS  |  {}  |  {}",
                timeline->getEventCount(),
                timeline->isCBFModeEnabled() ? "CBF ON" : "CBF OFF",
                timeline->getSelectedEventIndex() >= 0 ? "SELECTED" : "NO SELECTION"
            ).c_str()
        );

        const float maxWidth =
            timelineLayer ? timelineLayer->getContentSize().width - 185.0f : 300.0f;
        const float natural = timelineInfoLabel->getContentSize().width;

        if (natural > 0.0f)
            timelineInfoLabel->setScale(std::min(0.31f, maxWidth / natural));
    }
}

void MacroTimelineLayer::updateSubframeCounter() {
    double subframe = timeline->getPlayheadSubframe();
    int percent = static_cast<int>(subframe * 100);
    subframeLabel->setString(fmt::format("{:02d}%", percent).c_str());
}

void MacroTimelineLayer::scheduleUpdate() {
    this->schedule(schedule_selector(MacroTimelineLayer::updateTimeline), 0.016f);  // ~60 FPS
}

// Event handlers
void MacroTimelineLayer::onPlayPressed(CCObject*) {
    if (!timeline || timeline->getEventCount() == 0)
        return;

    auto& g = Global::get();
    auto* pl = PlayLayer::get();

    if (g.state == state::playing) {
        toolbarPlaying = true;
        if (pl && pl->m_isPaused)
            pl->pauseGame(true);
        return;
    }

    g.state = state::playing;
    g.currentAction = 0;
    g.currentFrameFix = 0;
    g.restart = false;
    g.firstAttempt = false;
    g.respawnFrame = -1;
    g.stopPlaying = false;
    g.macro.xdBotMacro = g.macro.botInfo.name == "xdBot";

    cbf::Engine::get()->reset();

    if (pl) {
        if (!pl->m_isPaused && !pl->m_levelEndAnimationStarted) {
            if (pl->m_levelSettings->m_platformerMode)
                pl->resetLevelFromStart();
            else
                pl->resetLevel();
        } else {
            g.restart = true;
        }
    }

    Macro::updateTPS();

    toolbarPlaying = true;
}

void MacroTimelineLayer::onPausePressed(CCObject*) {
    auto& g = Global::get();
    auto* pl = PlayLayer::get();

    if (g.state == state::playing)
        g.state = state::none;

    g.restart = false;
    cbf::Engine::get()->reset();
    Macro::updateTPS();

    if (pl && !pl->m_isPaused)
        pl->pauseGame(false);

    toolbarPlaying = false;
}

void MacroTimelineLayer::onStopPressed(CCObject*) {
    Macro::resetState();
    cbf::Engine::get()->reset();
    timeline->setPlayhead(0, 0.0);
    toolbarPlaying = false;
}

void MacroTimelineLayer::onStepFramePressed(CCObject*) {
    if (!timeline) return;
    timeline->setPlayhead(timeline->getPlayheadFrame() + 1, timeline->getPlayheadSubframe());
}

void MacroTimelineLayer::onCBFTogglePressed(CCObject*) {
    if (!timeline)
        return;

    bool enabled = Mod::get()->getSavedValue<bool>("macro_cbf", true);
    Mod::get()->setSavedValue("macro_cbf", !enabled);
    timeline->toggleCBFMode();

    if (cbfModeToggle) {
        cbfModeToggle->setOpacity(!enabled ? 255 : 140);
    }
}

void MacroTimelineLayer::onZoomInPressed(CCObject*) {
    timeline->zoom(25);
    renderState.pixelsPerFrame = timeline->getZoomLevel() / 100.0f * 4.0f;
}

void MacroTimelineLayer::onZoomOutPressed(CCObject*) {
    timeline->zoom(-25);
    renderState.pixelsPerFrame = timeline->getZoomLevel() / 100.0f * 4.0f;
}

void MacroTimelineLayer::onUndoPressed(CCObject*) {
    if (timeline && timeline->canUndo())
        timeline->undo();
}

void MacroTimelineLayer::onRedoPressed(CCObject*) {
    if (timeline && timeline->canRedo())
        timeline->redo();
}

// Input handling
bool MacroTimelineLayer::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (!FLAlertLayer::ccTouchBegan(touch, event)) return false;
    if (!timeline) return true;

    const CCPoint eventPos = eventsLayer->convertToNodeSpace(touch->getLocation());
    const int eventIdx = hitTestEvent(eventPos);
    if (eventIdx >= 0) {
        timeline->selectEvent(eventIdx);
        inputState.isDragging = true;
        inputState.draggedEventIdx = eventIdx;
        inputState.dragStartX = eventPos.x;
        inputState.dragStartPreciseFrame = timeline->getEvent(eventIdx)->getPreciseFrame();
        return true;
    }

    const CCPoint timelinePos = timelineLayer->convertToNodeSpace(touch->getLocation());
    const bool insideTimeline =
        timelinePos.x >= 0.0f &&
        timelinePos.x <= renderState.timelineSize.width &&
        timelinePos.y >= 20.0f &&
        timelinePos.y <= renderState.timelineSize.height;

    if (!insideTimeline)
        return true;

    // Ruler clicks seek. Empty track space pans horizontally.
    if (timelinePos.y >= renderState.timelineSize.height - renderState.rulerHeight) {
        timeline->setPlayheadPrecise(
            (timelinePos.x + timeline->getScrollOffset()) /
            renderState.pixelsPerFrame
        );
        return true;
    }

    inputState.isDragging = true;
    inputState.draggedEventIdx = -2;
    inputState.dragStartX = timelinePos.x;
    inputState.dragStartPreciseFrame =
        static_cast<double>(timeline->getScrollOffset());

    return true;
}

void MacroTimelineLayer::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    FLAlertLayer::ccTouchMoved(touch, event);

    if (!inputState.isDragging)
        return;

    if (inputState.draggedEventIdx == -2) {
        const CCPoint pos = timelineLayer->convertToNodeSpace(touch->getLocation());
        const float delta = pos.x - inputState.dragStartX;

        timeline->setScrollOffset(
            std::max(
                0,
                static_cast<int>(
                    inputState.dragStartPreciseFrame - delta
                )
            )
        );
        return;
    }

    if (inputState.draggedEventIdx < 0)
        return;

    const CCPoint pos = eventsLayer->convertToNodeSpace(touch->getLocation());
    double precise =
        (pos.x + timeline->getScrollOffset()) /
        renderState.pixelsPerFrame;

    if (precise < 0.0)
        precise = 0.0;

    if (const auto* evt = timeline->getEvent(inputState.draggedEventIdx)) {
        timeline->setEventFrame(
            inputState.draggedEventIdx,
            static_cast<int>(std::floor(precise))
        );

        if (timeline->isCBFModeEnabled()) {
            timeline->setEventSubframe(
                inputState.draggedEventIdx,
                precise - std::floor(precise)
            );
        }
    }
}

void MacroTimelineLayer::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    FLAlertLayer::ccTouchEnded(touch, event);
    inputState.isDragging = false;
    inputState.draggedEventIdx = -1;
}

// Helper functions
int MacroTimelineLayer::hitTestEvent(const CCPoint& pos) {
    for (int i = timeline->getEventCount() - 1; i >= 0; --i) {
        CCRect rect = getEventRenderRect(i);
        if (rect.containsPoint(pos)) {
            return i;
        }
    }
    return -1;
}

CCPoint MacroTimelineLayer::getEventRenderPos(int eventIndex) {
    if (eventIndex < 0 || eventIndex >= timeline->getEventCount()) return {0, 0};

    const auto* evt = timeline->getEvent(eventIndex);
    if (!evt) return {0, 0};

    const float x = frameToPixels(evt->frame, evt->subframe);
    const float y = evt->player2
        ? renderState.trackHeight * 0.5f
        : renderState.trackHeight * 1.5f;

    return {x, y};
}

CCRect MacroTimelineLayer::getEventRenderRect(int eventIndex) {
    const auto pos = getEventRenderPos(eventIndex);
    return CCRectMake(pos.x - 9.0f, pos.y - 10.0f, 18.0f, 20.0f);
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
    const float ppf = std::max(0.5f, renderState.pixelsPerFrame);

    float contentFrames = 180.0f;
    if (macro && !macro->inputs.empty()) {
        contentFrames = std::max(
            contentFrames,
            static_cast<float>(macro->inputs.back().getPreciseFrame()) + 48.0f
        );
    }

    const int maxScroll = std::max(
        0,
        static_cast<int>(
            std::ceil(
                contentFrames * ppf -
                renderState.timelineSize.width +
                24.0f
            )
        )
    );

    int scroll = std::clamp(timeline->getScrollOffset(), 0, maxScroll);

    const float playheadX = frameToPixels(
        timeline->getPlayheadFrame(),
        timeline->getPlayheadSubframe()
    );

    if (playheadX < 40.0f || playheadX > renderState.timelineSize.width - 40.0f) {
        const double precise =
            static_cast<double>(timeline->getPlayheadFrame()) +
            timeline->getPlayheadSubframe();

        scroll = std::clamp(
            static_cast<int>(
                precise * ppf - renderState.timelineSize.width * 0.5f
            ),
            0,
            maxScroll
        );
    }

    timeline->setScrollOffset(scroll);
}

void MacroTimelineLayer::updateInspectorPanel() {
    if (!inspectorBg || !inspectorBg->isVisible())
        return;
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
