/* Copyright (c) 2021, Jean Pierre Cimalando
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the documentation
 *       and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TapEditScreen.h"
#include "TapSlider.h"
#include "editor/utility/FunctionalTimer.h"
#include <GdJuce.h>
#include <Gd.h>
#include <chrono>
#include <utility>
namespace kro = std::chrono;

struct TapEditScreen::Impl : public TapEditItem::Listener,
                             public TapMiniMap::Listener,
                             public juce::ChangeListener {
    using Listener = TapEditScreen::Listener;

    TapEditScreen *self_ = nullptr;
    juce::ListenerList<Listener> listeners_;

    ///
    std::unique_ptr<TapEditItem> items_[GdMaxLines];
    TapMiniMap *miniMap_ = nullptr;
    juce::Range<float> timeRange_{0, 5};
    TapEditMode editMode_ = kTapEditOff;

    bool sync_ = true;
    int div_ = GdDefaultDivisor;
    float swing_ = 0.5f;
    double bpm_ = 120.0;

    enum {
        xMargin = 10,
        yMargin = 10,
    };

    ///
    bool tapHasBegun_ = false;
    unsigned tapCaptureCount_ = 0;
    kro::steady_clock::time_point tapBeginTime_;
    std::unique_ptr<juce::Timer> tapCaptureTimer_;

    ///
    std::unique_ptr<juce::Label> timeRangeLabel_[2];

    ///
    std::unique_ptr<juce::Timer> miniMapUpdateTimer_;

    ///
    class TapLassoSource : public juce::LassoSource<TapEditItem *> {
    public:
        explicit TapLassoSource(Impl &impl);
        void findLassoItemsInArea(juce::Array<TapEditItem *> &itemsFound, const juce::Rectangle<int> &area) override;
        juce::SelectedItemSet<TapEditItem *> &getLassoSelection() override;
    private:
        Impl *impl_ = nullptr;
    };
    using TapLassoComponent = juce::LassoComponent<TapEditItem *>;
    std::unique_ptr<TapLassoComponent> lasso_;
    std::unique_ptr<TapLassoSource> lassoSource_;
    juce::SelectedItemSet<TapEditItem *> lassoSelection_;

    ///
    juce::MouseCursor pencilCursor_;
    juce::ModifierKeys pencilModifiers_;

    ///
    enum {
        kStatusNormal,
        kStatusClicked,
        kStatusPencil,
        kStatusLasso,
    };
    int status_ = kStatusNormal;

    ///
    float delayToX(float t) const noexcept;
    float xToDelay(float x) const noexcept;
    float currentTapTime(kro::steady_clock::time_point now = kro::steady_clock::now()) const noexcept;
    int findUnusedTap() const;
    void createNewTap(int tapNumber, float delay);
    void clearAllTaps();
    void beginTapCapture();
    void nextTapCapture();
    void tickTapCapture();
    void endTapCapture();
    void autoZoomTimeRange();
    void updateItemSizeAndPosition(int itemNumber);
    void updateAllItemSizesAndPositions();
    void relayoutSubcomponents();
    void updateTimeRangeLabels();
    void scheduleUpdateMiniMap();
    void updateMiniMap();
    void pencilAt(juce::Point<float> position, juce::ModifierKeys mods);

    ///
    void tapEditStarted(TapEditItem *item, GdParameter id) override;
    void tapEditEnded(TapEditItem *item, GdParameter id) override;
    void tapValueChanged(TapEditItem *item, GdParameter id, float value) override;

    ///
    void miniMapRangeChanged(TapMiniMap *, juce::Range<float> range) override;

    ///
    void changeListenerCallback(juce::ChangeBroadcaster *source) override;
};

TapEditScreen::TapEditScreen()
    : impl_(new Impl)
{
    Impl &impl = *impl_;
    impl.self_ = this;

    setWantsKeyboardFocus(true);

    for (int itemNumber = 0; itemNumber < GdMaxLines; ++itemNumber) {
        TapEditItem *item = new TapEditItem(this, itemNumber);
        impl.items_[itemNumber].reset(item);
        item->addListener(&impl);
        addChildComponent(item);
    }

    Impl::TapLassoComponent *lasso = new Impl::TapLassoComponent;
    impl.lasso_.reset(lasso);
    addChildComponent(lasso);
    lasso->setColour(Impl::TapLassoComponent::lassoFillColourId, findColour(lassoFillColourId));
    lasso->setColour(Impl::TapLassoComponent::lassoOutlineColourId, findColour(lassoOutlineColourId));
    Impl::TapLassoSource *lassoSource = new Impl::TapLassoSource(impl);
    impl.lassoSource_.reset(lassoSource);
    impl.lassoSelection_.addChangeListener(&impl);

    for (int i = 0; i < 2; ++i) {
        juce::Label *label = new juce::Label;
        label->setSize(100, 24);
        label->setColour(juce::Label::textColourId, findColour(textColourId));
        impl.timeRangeLabel_[i].reset(label);
        addAndMakeVisible(label);
    }
    impl.timeRangeLabel_[0]->setJustificationType(juce::Justification::left);
    impl.timeRangeLabel_[1]->setJustificationType(juce::Justification::right);

    impl.tapCaptureTimer_.reset(FunctionalTimer::create([&impl]() { impl.tickTapCapture(); }));

    impl.updateTimeRangeLabels();
    impl.relayoutSubcomponents();

    if (LookAndFeelMethods *lm = dynamic_cast<LookAndFeelMethods *>(&getLookAndFeel())) {
        impl.pencilCursor_ = lm->createPencilCursor();
    }
}

TapEditScreen::~TapEditScreen()
{
    disconnectMiniMap();

    Impl &impl = *impl_;
    impl.lassoSelection_.removeChangeListener(&impl);
}

void TapEditScreen::connectMiniMap(TapMiniMap &miniMap)
{
    Impl &impl = *impl_;

    if (impl.miniMap_ && impl.miniMap_ != &miniMap)
        disconnectMiniMap();

    impl.miniMap_ = &miniMap;

    miniMap.setTimeRange(impl.timeRange_, juce::dontSendNotification);
    miniMap.addListener(&impl);
    impl.miniMapUpdateTimer_.reset(FunctionalTimer::create([&impl]() { impl.updateMiniMap(); }));
    impl.scheduleUpdateMiniMap();
}

void TapEditScreen::disconnectMiniMap()
{
    Impl &impl = *impl_;
    TapMiniMap *miniMap = impl.miniMap_;

    if (!miniMap)
        return;

    impl.miniMapUpdateTimer_.reset();
    miniMap->removeListener(&impl);
    impl.miniMap_ = nullptr;
}

TapEditMode TapEditScreen::getEditMode() const noexcept
{
    Impl &impl = *impl_;
    return impl.editMode_;
}

void TapEditScreen::setEditMode(TapEditMode mode)
{
    Impl &impl = *impl_;
    if (impl.editMode_ == mode)
        return;

    impl.editMode_ = mode;
    for (int itemNumber = 0; itemNumber < GdMaxLines; ++itemNumber) {
        TapEditItem &item = *impl.items_[itemNumber];
        const TapEditData &data = item.getData();
        item.setEditMode(data.enabled ? mode : kTapEditOff);
    }

    impl.scheduleUpdateMiniMap();
    repaint();
}

juce::Range<float> TapEditScreen::getTimeRange() const noexcept
{
    Impl &impl = *impl_;
    return impl.timeRange_;
}

void TapEditScreen::setTimeRange(juce::Range<float> newTimeRange)
{
    Impl &impl = *impl_;
    if (impl.timeRange_ == newTimeRange)
        return;

    impl.timeRange_ = newTimeRange;
    impl.updateAllItemSizesAndPositions();

    if (TapMiniMap *miniMap = impl.miniMap_)
        miniMap->setTimeRange(impl.timeRange_, juce::dontSendNotification);

    impl.updateTimeRangeLabels();
    repaint();
}

float TapEditScreen::getTapValue(GdParameter id) const
{
    int tapNumber;
    GdDecomposeParameter(id, &tapNumber);

    Impl &impl = *impl_;

    switch ((int)id) {
    default:
        if (tapNumber != -1) {
            TapEditItem &item = *impl.items_[tapNumber];
            return item.getTapValue(id);
        }
        return 0;
    case GDP_SYNC:
        return impl.sync_;
    case GDP_GRID:
        return (float)impl.div_;
    case GDP_SWING:
        return impl.swing_ * 100.0f;
    }
}

void TapEditScreen::setTapValue(GdParameter id, float value, juce::NotificationType nt)
{
    int tapNumber;
    GdDecomposeParameter(id, &tapNumber);

    Impl &impl = *impl_;

    switch ((int)id) {
    default:
        if (tapNumber != -1) {
            TapEditItem &item = *impl.items_[tapNumber];
            item.setTapValue(id, value, nt);
        }
        break;
    case GDP_SYNC:
        impl.sync_ = (bool)value;
        updateAllItemSizesAndPositions();
        repaint();
        break;
    case GDP_GRID:
        impl.div_ = GdFindNearestDivisor(value);
        updateAllItemSizesAndPositions();
        repaint();
        break;
    case GDP_SWING:
        impl.swing_ = value / 100.0f;
        updateAllItemSizesAndPositions();
        repaint();
        break;
    }

    impl.scheduleUpdateMiniMap();
}

bool TapEditScreen::isTapSelected(int tapNumber) const
{
    Impl &impl = *impl_;
    return impl.items_[tapNumber]->isTapSelected();
}

void TapEditScreen::setAllTapsSelected(bool selected)
{
    Impl &impl = *impl_;
    for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
        TapEditItem &item = *impl.items_[tapNumber];
        item.setTapSelected(selected);
    }
}

void TapEditScreen::setOnlyTapSelected(int selectedTapNumber)
{
    Impl &impl = *impl_;
    for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
        TapEditItem &item = *impl.items_[tapNumber];
        bool selected = tapNumber == selectedTapNumber;
        item.setTapSelected(selected);
        if (selected)
            item.toFront(false);
    }
}

double TapEditScreen::getBPM() const
{
    Impl &impl = *impl_;
    return impl.bpm_;
}

void TapEditScreen::setBPM(double bpm)
{
    Impl &impl = *impl_;
    if (impl.bpm_ == bpm)
        return;

    impl.bpm_ = bpm;
    impl.updateAllItemSizesAndPositions();
    repaint();
}

void TapEditScreen::beginTap()
{
    Impl &impl = *impl_;

    if (!impl.tapHasBegun_)
        impl.beginTapCapture();
    else
        impl.nextTapCapture();

    repaint();
}

void TapEditScreen::endTap()
{
    Impl &impl = *impl_;

    if (!impl.tapHasBegun_)
        return;

    impl.nextTapCapture();
    impl.endTapCapture();

    repaint();
}

int TapEditScreen::Impl::findUnusedTap() const
{
    int selectedNumber = -1;

    for (int itemNumber = 0; itemNumber < GdMaxLines && selectedNumber == -1; ++itemNumber) {
        TapEditItem &item = *items_[itemNumber];
        const TapEditData &data = item.getData();
        if (!data.enabled)
            selectedNumber = itemNumber;
    }

    return selectedNumber;
}

void TapEditScreen::Impl::createNewTap(int tapNumber, float delay)
{
    TapEditScreen *self = self_;

    for (int i = 0; i < GdNumPametersPerTap; ++i) {
        GdParameter decomposedId = (GdParameter)(GdFirstParameterOfFirstTap + i);
        GdParameter id = GdRecomposeParameter(decomposedId, tapNumber);

        switch ((int)decomposedId) {
        case GDP_TAP_A_ENABLE:
            self->setTapValue(id, true);
            break;
        case GDP_TAP_A_DELAY:
            self->setTapValue(id, delay);
            break;
        default:
            self->setTapValue(id, GdParameterDefault(id));
            break;
        }
    }
}

void TapEditScreen::Impl::clearAllTaps()
{
    TapEditScreen *self = self_;

    for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
        GdParameter id = GdRecomposeParameter(GDP_TAP_A_ENABLE, tapNumber);
        self->setTapValue(id, false);
    }
}

void TapEditScreen::Impl::beginTapCapture()
{
    TapEditScreen *self = self_;
    self->setTimeRange({0, GdMaxDelay});
    tapHasBegun_ = true;
    tapCaptureCount_ = 0;
    tapBeginTime_ = kro::steady_clock::now();
    tapCaptureTimer_->startTimerHz(60);
    listeners_.call([self](Listener &l) { l.tappingHasStarted(self); });
    self->grabKeyboardFocus();
}

void TapEditScreen::Impl::nextTapCapture()
{
    float delay = currentTapTime();
    if (delay > (float)GdMaxDelay)
        return;

    TapEditScreen *self = self_;
    delay = self->alignDelayToGrid(delay);

    if (tapCaptureCount_ == 0)
        clearAllTaps();

    int nextTapNumber = findUnusedTap();
    if (nextTapNumber == -1)
        return;

    createNewTap(nextTapNumber, delay);
    ++tapCaptureCount_;
}

void TapEditScreen::Impl::tickTapCapture()
{
    TapEditScreen *self = self_;

    if (currentTapTime() > (float)GdMaxDelay)
        endTapCapture();

    self->repaint();
}

void TapEditScreen::Impl::endTapCapture()
{
    TapEditScreen *self = self_;
    tapCaptureTimer_->stopTimer();
    tapHasBegun_ = false;
    listeners_.call([self](Listener &l) { l.tappingHasEnded(self); });

    autoZoomTimeRange();
}

void TapEditScreen::Impl::autoZoomTimeRange()
{
    int count = 0;
    float maxDelay = 0;

    for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
        TapEditItem &item = *items_[tapNumber];

        bool enable = (bool)item.getTapValue(GdRecomposeParameter(GDP_TAP_A_ENABLE, tapNumber));
        float delay = item.getTapValue(GdRecomposeParameter(GDP_TAP_A_DELAY, tapNumber));

        if (enable) {
            maxDelay = std::max(delay, maxDelay);
            ++count;
        }
    }

    if (count == 0)
        maxDelay = GdMaxDelay;
    else
    {
        const float interval = 0.5f;
        maxDelay = std::min((float)GdMaxDelay, interval * std::floor((maxDelay + interval) / interval));
    }

    TapEditScreen *self = self_;
    self->setTimeRange({0, maxDelay});
}

void TapEditScreen::updateItemSizeAndPosition(int tapNumber)
{
    Impl &impl = *impl_;
    impl.updateItemSizeAndPosition(tapNumber);
}

void TapEditScreen::updateAllItemSizesAndPositions()
{
    Impl &impl = *impl_;
    impl.updateAllItemSizesAndPositions();
}

float TapEditScreen::getXForDelay(float delay) const
{
    Impl &impl = *impl_;
    return impl.delayToX(delay);
}

float TapEditScreen::getDelayForX(float x) const
{
    Impl &impl = *impl_;
    return impl.xToDelay(x);
}

float TapEditScreen::alignDelayToGrid(float delay) const
{
    Impl &impl = *impl_;
    float newDelay;
    if (!impl.sync_)
        newDelay = juce::jlimit(0.0f, (float)GdMaxDelay, delay);
    else
        newDelay = GdAlignDelayToGrid(delay, impl.div_, impl.swing_, (float)impl.bpm_);
    return newDelay;
}

void TapEditScreen::autoZoomTimeRange()
{
    Impl &impl = *impl_;
    impl.autoZoomTimeRange();
}

juce::Rectangle<int> TapEditScreen::getLocalBoundsNoMargin() const
{
    return getLocalBounds().reduced(Impl::xMargin, Impl::yMargin);
}

juce::Rectangle<int> TapEditScreen::getScreenArea() const
{
    return getIntervalsRow().getUnion(getSlidersRow()).getUnion(getButtonsRow());
}

juce::Rectangle<int> TapEditScreen::getButtonsRow() const
{
    int buttonsHeight = TapEditItem::getLabelHeight();
    return getLocalBoundsNoMargin().removeFromTop(buttonsHeight);
}

juce::Rectangle<int> TapEditScreen::getIntervalsRow() const
{
    int intervalsHeight = TapEditItem::getLabelHeight();
    return getLocalBoundsNoMargin().removeFromBottom(intervalsHeight);
}

juce::Rectangle<int> TapEditScreen::getSlidersRow() const
{
    int buttonsHeight = TapEditItem::getLabelHeight();
    int intervalsHeight = TapEditItem::getLabelHeight();
    return getLocalBoundsNoMargin().withTrimmedBottom(intervalsHeight).withTrimmedTop(buttonsHeight);
}

juce::Colour TapEditScreen::getColourOfEditMode(const juce::LookAndFeel &lnf, TapEditMode mode)
{
    juce::Colour modeColour;

    switch (mode) {
    default:
    case kTapEditOff:
        break;
    case kTapEditCutoff:
        modeColour = lnf.findColour(TapEditScreen::editCutoffBaseColourId);
        break;
    case kTapEditResonance:
        modeColour = lnf.findColour(TapEditScreen::editResonanceBaseColourId);
        break;
    case kTapEditTune:
        modeColour = lnf.findColour(TapEditScreen::editTuneBaseColourId);
        break;
    case kTapEditPan:
        modeColour = lnf.findColour(TapEditScreen::editPanBaseColourId);
        break;
    case kTapEditLevel:
        modeColour = lnf.findColour(TapEditScreen::editLevelBaseColourId);
        break;
    }

    return modeColour;
}

void TapEditScreen::addListener(Listener *listener)
{
    Impl &impl = *impl_;
    impl.listeners_.add(listener);
}

void TapEditScreen::removeListener(Listener *listener)
{
    Impl &impl = *impl_;
    impl.listeners_.remove(listener);
}

void TapEditScreen::paint(juce::Graphics &g)
{
    juce::Component::paint(g);

    Impl &impl = *impl_;
    juce::Rectangle<int> screenBounds = getScreenArea();
    juce::Rectangle<int> buttonsRow = getButtonsRow();
    juce::Rectangle<int> intervalsRow = getIntervalsRow();

    juce::Colour screenContourColour = findColour(screenContourColourId);
    juce::Colour intervalFillColour = findColour(intervalFillColourId);
    juce::Colour intervalContourColour = findColour(intervalContourColourId);
    juce::Colour minorIntervalTickColour = findColour(minorIntervalTickColourId);
    juce::Colour majorIntervalTickColour = findColour(majorIntervalTickColourId);
    juce::Colour superMajorIntervalTickColour = findColour(superMajorIntervalTickColourId);

    g.setColour(screenContourColour);
    g.drawRect(screenBounds);
    g.setColour(intervalFillColour);
    g.fillRect(intervalsRow);
    if (impl.sync_) {
        int div = impl.div_;
        int majorDiv = div / ((div & 3) ? 2 : 4);
        int superMajorDiv = div;
        float swing = impl.swing_;
        float bpm = (float)impl.bpm_;
        for (int i = 0; ; ++i) {
            float d = GdGetGridTick(i, div, swing, bpm);
            float x = getXForDelay(d);
            if (x < (float)screenBounds.getX()) continue;
            if (x > (float)intervalsRow.getRight()) break;
            g.setColour((i % superMajorDiv == 0) ? superMajorIntervalTickColour :
                        (i % majorDiv == 0) ? majorIntervalTickColour :
                        minorIntervalTickColour);
            g.drawLine(x, (float)(intervalsRow.getY() + 1), x, (float)(intervalsRow.getBottom() - 1));
            if (d >= (float)GdMaxDelay) break;
        }
    }
    g.setColour(intervalContourColour);
    g.drawRect(intervalsRow);

    ///
    g.setColour(intervalFillColour);
    g.fillRect(buttonsRow);
    g.setColour(intervalContourColour);
    g.drawRect(buttonsRow);

    ///
    float refLineY = 0;
    if (impl.items_[0]->getReferenceLineY(impl.editMode_, refLineY, this)) {
        juce::Colour lineColour = findColour(lineColourId);
        g.setColour(lineColour);
        g.drawHorizontalLine((int)(refLineY + 0.5f), (float)(screenBounds.getX() + 1), (float)(screenBounds.getRight() - 1));
    }

    if (impl.tapHasBegun_) {
        juce::Colour tapLineColour = findColour(tapLineColourId);
        float tapLineX = impl.delayToX(impl.currentTapTime());

        g.setColour(tapLineColour);
        g.drawLine(tapLineX, (float)screenBounds.getY() + 1, tapLineX, (float)screenBounds.getBottom() - 1);
    }
}

void TapEditScreen::mouseDown(const juce::MouseEvent &e)
{
    Impl &impl = *impl_;
    juce::Rectangle<int> intervalsRow = getIntervalsRow();

    if (impl.status_ == Impl::kStatusNormal) {
        if (intervalsRow.toFloat().contains(e.position)) {
            float delay = alignDelayToGrid(getDelayForX(e.position.getX()));
            int tapNumber = impl.findUnusedTap();
            if (tapNumber != -1) {
                impl.createNewTap(tapNumber, delay);
                setOnlyTapSelected(tapNumber);
            }
        }
        else if (e.mods.isShiftDown()) {
            setMouseCursor(impl.pencilCursor_);
            impl.status_ = Impl::kStatusPencil;
            impl.pencilModifiers_ = e.mods;
            impl.pencilAt(e.position, e.mods);
        }
        else {
            impl.status_ = Impl::kStatusClicked;
        }
    }
}

void TapEditScreen::mouseUp(const juce::MouseEvent &e)
{
    Impl &impl = *impl_;

    (void)e;

    switch (impl.status_) {
    case Impl::kStatusClicked:
        setAllTapsSelected(false);
        impl.status_ = Impl::kStatusNormal;
        break;
    case Impl::kStatusPencil:
        setMouseCursor(juce::MouseCursor::NormalCursor);
        impl.status_ = Impl::kStatusNormal;
        break;
    case Impl::kStatusLasso:
        impl.lasso_->endLasso();
        impl.status_ = Impl::kStatusNormal;
        break;
    }
}

void TapEditScreen::mouseMove(const juce::MouseEvent &e)
{
    Impl &impl = *impl_;

    switch (impl.status_) {
    case Impl::kStatusNormal:
        if (getIntervalsRow().toFloat().contains(e.position))
            setMouseCursor(impl.pencilCursor_);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
        break;
    }
}

void TapEditScreen::mouseDrag(const juce::MouseEvent &e)
{
    Impl &impl = *impl_;

    switch (impl.status_) {
    case Impl::kStatusClicked:
        impl.lasso_->beginLasso(e, impl.lassoSource_.get());
        impl.status_ = Impl::kStatusLasso;
        break;
    case Impl::kStatusPencil:
        impl.pencilAt(e.position, impl.pencilModifiers_);
        break;
    case Impl::kStatusLasso:
        impl.lasso_->dragLasso(e);
        break;
    }
}

bool TapEditScreen::keyPressed(const juce::KeyPress &e)
{
    Impl &impl = *impl_;

    if (e.isKeyCode(juce::KeyPress::deleteKey)) {
        bool selected[GdMaxLines];

        for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber)
            selected[tapNumber] = isTapSelected(tapNumber);

        for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
            if (selected[tapNumber]) {
                GdParameter id = GdRecomposeParameter(GDP_TAP_A_ENABLE, tapNumber);
                setTapValue(id, false);
            }
        }

        setAllTapsSelected(false);
        return true;
    }
    else if (e.isKeyCode(juce::KeyPress::escapeKey)) {
        if (impl.tapHasBegun_)
            impl.endTapCapture();
        return true;
    }
    return false;
}

void TapEditScreen::resized()
{
    Impl &impl = *impl_;
    impl.relayoutSubcomponents();
}

float TapEditScreen::Impl::delayToX(float t) const noexcept
{
    TapEditScreen *self = self_;
    juce::Rectangle<float> rc = self->getLocalBoundsNoMargin().toFloat().reduced((float)TapEditItem::getLabelWidth() / 2.0f, 0);
    juce::Range<float> tr = timeRange_;
    return rc.getX() + rc.getWidth() * ((t - tr.getStart()) / tr.getLength());
}

float TapEditScreen::Impl::xToDelay(float x) const noexcept
{
    TapEditScreen *self = self_;
    juce::Rectangle<float> rc = self->getLocalBoundsNoMargin().toFloat().reduced((float)TapEditItem::getLabelWidth() / 2.0f, 0);
    juce::Range<float> tr = timeRange_;
    return tr.getStart() + tr.getLength() * ((x - rc.getX()) / rc.getWidth());
}

float TapEditScreen::Impl::currentTapTime(kro::steady_clock::time_point now) const noexcept
{
    kro::steady_clock::duration dur = now - tapBeginTime_;
    float secs = kro::duration<float>(dur).count();
    return secs;
}

void TapEditScreen::Impl::updateItemSizeAndPosition(int itemNumber)
{
    TapEditScreen *self = self_;
    juce::Rectangle<int> screenBounds = self->getScreenArea();
    TapEditItem &item = *items_[itemNumber];
    const TapEditData &data = item.getData();
    int width = item.getLabelWidth();
    int height = screenBounds.getHeight();
    item.setSize(width, height);
    float delay = data.delay;
    if (sync_)
        delay = GdAlignDelayToGrid(delay, div_, swing_, (float)bpm_);
    item.setTopLeftPosition((int)(delayToX(delay) - 0.5f * (float)width), screenBounds.getY());
}

void TapEditScreen::Impl::updateAllItemSizesAndPositions()
{
    for (int itemNumber = 0; itemNumber < GdMaxLines; ++itemNumber)
        updateItemSizeAndPosition(itemNumber);
}

void TapEditScreen::Impl::relayoutSubcomponents()
{
    updateAllItemSizesAndPositions();

    TapEditScreen *self = self_;
    juce::Rectangle<int> intervalsRow = self->getIntervalsRow();

    juce::Point<int> timeRangeLabelPos[2] = {
        intervalsRow.getTopLeft().translated(0.0f, -timeRangeLabel_[0]->getHeight()),
        intervalsRow.getTopRight().translated(0.0f, -timeRangeLabel_[1]->getHeight()),
    };
    timeRangeLabel_[0]->setTopLeftPosition(timeRangeLabelPos[0].getX(), timeRangeLabelPos[0].getY());
    timeRangeLabel_[1]->setTopRightPosition(timeRangeLabelPos[1].getX(), timeRangeLabelPos[1].getY());
}

void TapEditScreen::Impl::updateTimeRangeLabels()
{
    int t1ms = juce::roundToInt(1000 * timeRange_.getStart());
    int t2ms = juce::roundToInt(1000 * timeRange_.getEnd());
    timeRangeLabel_[0]->setText(juce::String(t1ms) + " ms", juce::dontSendNotification);
    timeRangeLabel_[1]->setText(juce::String(t2ms) + " ms", juce::dontSendNotification);
}

void TapEditScreen::Impl::scheduleUpdateMiniMap()
{
    if (miniMapUpdateTimer_)
        miniMapUpdateTimer_->startTimer(1);
}

void TapEditScreen::Impl::updateMiniMap()
{
    jassert(miniMap_);

    TapMiniMapValue miniMapValues[GdMaxLines];
    int numMiniMapValues = 0;

    for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
        TapEditItem &item = *items_[tapNumber];
        if ((bool)item.getTapValue(GdRecomposeParameter(GDP_TAP_A_ENABLE, tapNumber)))
            miniMapValues[numMiniMapValues++] = item.getMinimapValues();
    }

    TapMiniMap &miniMap = *miniMap_;
    miniMap.displayValues(miniMapValues, numMiniMapValues);

    miniMapUpdateTimer_->stopTimer();
}

void TapEditScreen::Impl::pencilAt(juce::Point<float> position, juce::ModifierKeys mods)
{
    TapEditScreen *self = self_;

    for (int itemNumber = 0; itemNumber < GdMaxLines; ++itemNumber) {
        TapEditItem &item = *items_[itemNumber];
        if (!item.isVisible())
            continue;
        juce::Rectangle<int> ib = item.getLocalBounds();
        juce::Point<int> pt = item.getLocalPoint(self, position).roundToInt();
        if (pt.getX() < 0 || pt.getX() > ib.getRight())
            continue;
        item.pencilAt(pt, mods);
    }
}

void TapEditScreen::Impl::tapEditStarted(TapEditItem *, GdParameter id)
{
    TapEditScreen *self = self_;
    listeners_.call([self, id](Listener &listener) { listener.tapEditStarted(self, id); });
}

void TapEditScreen::Impl::tapEditEnded(TapEditItem *, GdParameter id)
{
    TapEditScreen *self = self_;
    listeners_.call([self, id](Listener &listener) { listener.tapEditEnded(self, id); });
}

void TapEditScreen::Impl::tapValueChanged(TapEditItem *, GdParameter id, float value)
{
    TapEditScreen *self = self_;
    listeners_.call([self, id, value](Listener &listener) { listener.tapValueChanged(self, id, value); });
}

void TapEditScreen::Impl::miniMapRangeChanged(TapMiniMap *, juce::Range<float> range)
{
    TapEditScreen *self = self_;
    self->setTimeRange(range);
}

void TapEditScreen::Impl::changeListenerCallback(juce::ChangeBroadcaster *source)
{
    if (source == &lassoSelection_) {
        bool selected[GdMaxLines] = {};
        for (TapEditItem *item : lassoSelection_)
            selected[item->getItemNumber()] = true;

        for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
            TapEditItem &item = *items_[tapNumber];
            item.setTapSelected(selected[tapNumber]);
        }
    }
}

///
TapEditScreen::Impl::TapLassoSource::TapLassoSource(Impl &impl)
    : impl_(&impl)
{
}

void TapEditScreen::Impl::TapLassoSource::findLassoItemsInArea(juce::Array<TapEditItem *> &itemsFound, const juce::Rectangle<int> &area)
{
    Impl &impl = *impl_;
    for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
        TapEditItem &item = *impl.items_[tapNumber];
        if (item.isVisible() && area.intersects(item.getBounds()))
            itemsFound.add(&item);
    }
}

juce::SelectedItemSet<TapEditItem *> &TapEditScreen::Impl::TapLassoSource::getLassoSelection()
{
    Impl &impl = *impl_;
    return impl.lassoSelection_;
}

//------------------------------------------------------------------------------
struct TapEditItem::Impl : public TapSlider::Listener,
                           public juce::Button::Listener {
    using Listener = TapEditItem::Listener;

    TapEditItem *self_ = nullptr;
    juce::ListenerList<Listener> listeners_;
    juce::ComponentDragger dragger_;
    GdParameter dragChangeId_ = GDP_NONE;
    TapEditData data_;
    TapEditScreen *screen_ = nullptr;
    int itemNumber_ {};
    TapEditMode editMode_ = kTapEditOff;
    std::map<TapEditMode, std::unique_ptr<TapSlider>> sliders_;
    std::map<TapEditMode, std::unique_ptr<juce::Button>> buttons_;
    bool tapSelected_ = false;

    class Slider : public TapSlider {
    public:
        explicit Slider(TapEditItem::Impl &impl);
    protected:
        void paint(juce::Graphics &g) override;
    private:
        TapEditItem::Impl &impl_;
    };

    class Button : public juce::Button {
    public:
        explicit Button(TapEditItem::Impl &impl);
    protected:
        void paintButton(juce::Graphics &g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    private:
        TapEditItem::Impl &impl_;
    };

    TapSlider *getCurrentSlider() const;
    TapSlider *getSliderForEditMode(TapEditMode editMode) const;
    juce::Button *getCurrentButton() const;
    juce::Button *getButtonForEditMode(TapEditMode editMode) const;
    void updateSliderAndButtonVisibility();
    void updateSliderPolarities();
    void repositionSlidersAndButtons();
    juce::Rectangle<int> getLabelBounds() const;

    void sliderValueChanged(juce::Slider *slider) override;
    void sliderDragStarted(juce::Slider *slider) override;
    void sliderDragEnded(juce::Slider *slider) override;

    void buttonClicked(juce::Button *button) override;
};

TapEditItem::TapEditItem(TapEditScreen *screen, int itemNumber)
    : impl_(new Impl)
{
    Impl &impl = *impl_;
    impl.self_ = this;
    impl.itemNumber_ = itemNumber;
    impl.screen_ = screen;

    enum TapSliderKind {
        kTapSliderNormal,
        kTapSliderBipolar,
        kTapSliderTwoValues,
    };

    auto createSlider = [this, &impl]
        (TapEditMode mode, GdParameter id, GdParameter id2, int kind)
    {
        TapSlider *slider = new TapEditItem::Impl::Slider(impl);
        impl.sliders_[mode] = std::unique_ptr<TapSlider>(slider);
        GdRange range = GdParameterRange((GdParameter)id);
        float def = GdParameterDefault((GdParameter)id);
        slider->setNormalisableRange(GdJuceRange<double>(range));
        slider->setValue(def);
        slider->setDoubleClickReturnValue(true, def);
        if (kind == kTapSliderBipolar)
            slider->setBipolarAround(true, def);
        else if (kind == kTapSliderTwoValues)
            slider->setSliderStyle(juce::Slider::TwoValueVertical);
        slider->addListener(&impl);
        juce::NamedValueSet &properties = slider->getProperties();
        if (kind != kTapSliderTwoValues)
            properties.set("X-Change-ID", (int)id);
        else {
            properties.set("X-Change-ID-1", (int)id);
            properties.set("X-Change-ID-2", (int)id2);
        }
        juce::Colour modeColour = TapEditScreen::getColourOfEditMode(getLookAndFeel(), mode);
        slider->setColour(juce::Slider::backgroundColourId, findColour(TapEditScreen::tapSliderBackgroundColourId));
        slider->setColour(juce::Slider::trackColourId, modeColour.withAlpha(0.75f));
        addChildComponent(slider);
    };

    auto createButton = [this, &impl]
        (TapEditMode mode, GdParameter id)
    {
        juce::Button *button = new TapEditItem::Impl::Button(impl);
        impl.buttons_[mode] = std::unique_ptr<juce::Button>(button);
        button->addListener(&impl);
        juce::NamedValueSet &properties = button->getProperties();
        properties.set("X-Change-ID", (int)id);
        addChildComponent(button);
    };

    createSlider(kTapEditCutoff, GdRecomposeParameter(GDP_TAP_A_HPF_CUTOFF, itemNumber), GdRecomposeParameter(GDP_TAP_A_LPF_CUTOFF, itemNumber), kTapSliderTwoValues);
    createSlider(kTapEditResonance, GdRecomposeParameter(GDP_TAP_A_RESONANCE, itemNumber), GDP_NONE, kTapSliderNormal);
    createSlider(kTapEditTune, GdRecomposeParameter(GDP_TAP_A_TUNE, itemNumber), GDP_NONE, kTapSliderBipolar);
    createSlider(kTapEditPan, GdRecomposeParameter(GDP_TAP_A_PAN, itemNumber), GDP_NONE, kTapSliderBipolar);
    createSlider(kTapEditLevel, GdRecomposeParameter(GDP_TAP_A_LEVEL, itemNumber), GDP_NONE, kTapSliderNormal);

    createButton(kTapEditCutoff, GdRecomposeParameter(GDP_TAP_A_FILTER_ENABLE, itemNumber));
    createButton(kTapEditResonance, GdRecomposeParameter(GDP_TAP_A_FILTER, itemNumber));
    createButton(kTapEditTune, GdRecomposeParameter(GDP_TAP_A_TUNE_ENABLE, itemNumber));
    createButton(kTapEditPan, GdRecomposeParameter(GDP_TAP_A_FLIP, itemNumber));
    createButton(kTapEditLevel, GdRecomposeParameter(GDP_TAP_A_MUTE, itemNumber));
}

TapEditItem::~TapEditItem()
{
}

int TapEditItem::getItemNumber() const noexcept
{
    Impl &impl = *impl_;
    return impl.itemNumber_;
}

const TapEditData &TapEditItem::getData() const noexcept
{
    Impl &impl = *impl_;
    return impl.data_;
}

bool TapEditItem::getReferenceLineY(TapEditMode mode, float &lineY, juce::Component *relativeTo) const
{
    Impl &impl = *impl_;
    bool have = false;

    ///
    auto getSliderValueY = [](juce::Slider &slider, double value) -> double {
        double ratio = slider.valueToProportionOfLength(value);
        return slider.getBottom() - ratio * slider.getHeight();
    };

    ///
    switch ((int)mode) {
    case kTapEditCutoff:
        if (TapSlider *slider = impl.getSliderForEditMode(mode)) {
            lineY = (float)getSliderValueY(*slider, 1000.0);
            have = true;
        }
        break;
    case kTapEditResonance:
        if (TapSlider *slider = impl.getSliderForEditMode(mode)) {
            lineY = (float)getSliderValueY(*slider, 12.0);
            have = true;
        }
        break;
    case kTapEditTune:
    case kTapEditPan:
    case kTapEditLevel:
        if (TapSlider *slider = impl.getSliderForEditMode(mode)) {
            lineY = (float)getSliderValueY(*slider, 0.0);
            have = true;
        }
        break;
    }

    if (have && relativeTo)
        lineY = relativeTo->getLocalPoint(this, juce::Point<float>{0.0f, lineY}).getY();

    return have;
}

TapEditMode TapEditItem::getEditMode() const noexcept
{
    Impl &impl = *impl_;
    return impl.editMode_;
}

void TapEditItem::setEditMode(TapEditMode mode)
{
    Impl &impl = *impl_;
    if (impl.editMode_ == mode)
        return;

    impl.editMode_ = mode;

    impl.updateSliderAndButtonVisibility();

    repaint();
}

float TapEditItem::getTapValue(GdParameter id) const
{
    Impl &impl = *impl_;

    int tapNumber;
    GdParameter decomposedId = GdDecomposeParameter(id, &tapNumber);

    if (impl.itemNumber_ != tapNumber) {
        jassertfalse;
        return 0.0f;
    }

    switch ((int)decomposedId) {
    case GDP_TAP_A_ENABLE:
        return impl.data_.enabled;
    case GDP_TAP_A_DELAY:
        return impl.data_.delay;
    case GDP_TAP_A_LPF_CUTOFF:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditCutoff))
            return (float)slider->getMaxValue();
        goto notfound;
    case GDP_TAP_A_HPF_CUTOFF:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditCutoff))
            return (float)slider->getMinValue();
        goto notfound;
    case GDP_TAP_A_RESONANCE:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditResonance))
            return (float)slider->getValue();
        goto notfound;
    case GDP_TAP_A_TUNE:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditTune))
            return (float)slider->getValue();
        goto notfound;
    case GDP_TAP_A_PAN:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditPan))
            return (float)slider->getValue();
        goto notfound;
    case GDP_TAP_A_LEVEL:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditLevel))
            return (float)slider->getValue();
        goto notfound;
    case GDP_TAP_A_FILTER_ENABLE:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditCutoff))
            return (float)button->getToggleState();
        goto notfound;
    case GDP_TAP_A_FILTER:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditResonance))
            return (float)button->getToggleState();
        goto notfound;
    case GDP_TAP_A_TUNE_ENABLE:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditTune))
            return (float)button->getToggleState();
        goto notfound;
    case GDP_TAP_A_FLIP:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditPan))
            return (float)!button->getToggleState();
        goto notfound;
    case GDP_TAP_A_MUTE:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditLevel))
            return (float)!button->getToggleState();
        goto notfound;
    default: notfound:
        jassertfalse;
        return 0.0f;
    }
}

void TapEditItem::setTapValue(GdParameter id, float value, juce::NotificationType nt)
{
    Impl &impl = *impl_;

    int tapNumber;
    GdParameter decomposedId = GdDecomposeParameter(id, &tapNumber);

    if (impl.itemNumber_ != tapNumber) {
        jassertfalse;
        return;
    }

    switch ((int)decomposedId) {
    case GDP_TAP_A_ENABLE:
    {
        bool enabled = (bool)value;
        if (impl.data_.enabled == enabled)
            return;

        impl.data_.enabled = enabled;

        if (nt != juce::dontSendNotification)
            impl.listeners_.call([this, enabled](Listener &l) { l.tapValueChanged(this, GdRecomposeParameter(GDP_TAP_A_ENABLE, impl_->itemNumber_), enabled); });

        setVisible(enabled);

        TapEditScreen &screen = *impl.screen_;
        setEditMode(enabled ? screen.getEditMode() : kTapEditOff);
        if (enabled)
            screen.updateItemSizeAndPosition(impl.itemNumber_);

        break;
    }
    case GDP_TAP_A_DELAY:
    {
        float delay = value;
        if (impl.data_.delay == delay)
            return;

        impl.data_.delay = delay;

        if (nt != juce::dontSendNotification)
            impl.listeners_.call([this, delay](Listener &l) { l.tapValueChanged(this, GdRecomposeParameter(GDP_TAP_A_DELAY, impl_->itemNumber_), delay); });

        TapEditScreen &screen = *impl.screen_;
        if (impl.data_.enabled)
            screen.updateItemSizeAndPosition(impl.itemNumber_);

        break;
    }
    case GDP_TAP_A_LPF_CUTOFF:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditCutoff))
            slider->setMaxValue(value, nt);
        break;
    case GDP_TAP_A_HPF_CUTOFF:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditCutoff))
            slider->setMinValue(value, nt);
        break;
    case GDP_TAP_A_RESONANCE:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditResonance))
            slider->setValue(value, nt);
        break;
    case GDP_TAP_A_TUNE:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditTune))
            slider->setValue(value, nt);
        break;
    case GDP_TAP_A_PAN:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditPan))
            slider->setValue(value, nt);
        break;
    case GDP_TAP_A_LEVEL:
        if (TapSlider *slider = impl.getSliderForEditMode(kTapEditLevel))
            slider->setValue(value, nt);
        break;
    case GDP_TAP_A_FILTER_ENABLE:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditCutoff))
            button->setToggleState((bool)value, nt);
        break;
    case GDP_TAP_A_FILTER:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditResonance))
            button->setToggleState((bool)value, nt);
        break;
    case GDP_TAP_A_TUNE_ENABLE:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditTune))
            button->setToggleState((bool)value, nt);
        break;
    case GDP_TAP_A_FLIP:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditPan)) {
            button->setToggleState(!(bool)value, nt);
            impl.updateSliderPolarities();
        }
        break;
    case GDP_TAP_A_MUTE:
        if (juce::Button *button = impl.getButtonForEditMode(kTapEditLevel))
            button->setToggleState(!(bool)value, nt);
        break;
    }
}

TapMiniMapValue TapEditItem::getMinimapValues() const
{
    Impl &impl = *impl_;
    TapEditMode editMode = impl.editMode_;
    float start = 0;
    float end = 0;

    switch (editMode) {
    default:
    case kTapEditOff:
    fail:
        return {};
    // one-value sliders
    case kTapEditResonance:
    case kTapEditLevel:
        if (TapSlider *slider = impl.getSliderForEditMode(editMode)) {
            start = (float)slider->valueToProportionOfLength(slider->getMinimum());
            end = (float)slider->valueToProportionOfLength(slider->getValue());
            break;
        }
        goto fail;
    // two-value sliders
    case kTapEditCutoff:
        if (TapSlider *slider = impl.getSliderForEditMode(editMode)) {
            start = (float)slider->valueToProportionOfLength(slider->getMinValue());
            end = (float)slider->valueToProportionOfLength(slider->getMaxValue());
            break;
        }
        goto fail;
    // bipolar around zero
    case kTapEditTune:
    case kTapEditPan:
        if (TapSlider *slider = impl.getSliderForEditMode(editMode)) {
            start = 0.0f;
            end = (float)slider->getValue();
            if (end < start)
                std::swap(start, end);
            start = (float)slider->valueToProportionOfLength(start);
            end = (float)slider->valueToProportionOfLength(end);
            break;
        }
        goto fail;
    }

    TapMiniMapValue mmv;
    mmv.delay = getTapValue(GdRecomposeParameter(GDP_TAP_A_DELAY, impl.itemNumber_));
    mmv.range = juce::Range<float>{start, end};

    return mmv;
}

bool TapEditItem::isTapSelected() const
{
    Impl &impl = *impl_;
    return impl.tapSelected_;
}

void TapEditItem::setTapSelected(bool selected)
{
    Impl &impl = *impl_;

    if (impl.tapSelected_ == selected)
        return;

    impl.tapSelected_ = selected;
    repaint();
}

void TapEditItem::pencilAt(juce::Point<int> pos, juce::ModifierKeys mods)
{
    (void)mods;

    Impl &impl = *impl_;

    juce::Slider *slider = impl.getSliderForEditMode(impl.editMode_);
    if (!slider)
        return;

    juce::Point<int> sliderPos = slider->getLocalPoint(this, pos);
    double proportion = 1.0 - ((double)sliderPos.getY() / (double)slider->getHeight());
    proportion = juce::jlimit(0.0, 1.0, proportion);
    double value = slider->proportionOfLengthToValue(proportion);

    if (slider->isTwoValue()) {
        double distanceToMax = std::fabs(proportion - slider->valueToProportionOfLength(slider->getMaxValue()));
        double distanceToMin = std::fabs(proportion - slider->valueToProportionOfLength(slider->getMinValue()));
        if (distanceToMin < distanceToMax)
            slider->setMinValue(value);
        else
            slider->setMaxValue(value);
    }
    else if (slider->isThreeValue())
        jassertfalse;
    else
        slider->setValue(value);
}

void TapEditItem::addListener(Listener *listener)
{
    Impl &impl = *impl_;
    impl.listeners_.add(listener);
}

void TapEditItem::removeListener(Listener *listener)
{
    Impl &impl = *impl_;
    impl.listeners_.remove(listener);
}

void TapEditItem::paint(juce::Graphics &g)
{
    Impl &impl = *impl_;
    TapEditScreen *screen = impl.screen_;

    juce::Component::paint(g);

    juce::Colour tapLabelBackgroundColour =
        TapEditScreen::getColourOfEditMode(getLookAndFeel(), impl.editMode_);
    if (impl.tapSelected_)
        tapLabelBackgroundColour = tapLabelBackgroundColour.brighter(1.0f);

    juce::Colour tapLabelTextColour = findColour(TapEditScreen::tapLabelTextColourId);

    char labelTextCstr[2];
    labelTextCstr[0] = (char)(impl.itemNumber_ + 'A');
    labelTextCstr[1] = '\0';

    juce::Rectangle<int> labelBounds = impl.getLabelBounds();

    // clips painting within the screen area
    juce::Rectangle<int> clipBounds = getLocalArea(screen, screen->getScreenArea());
    g.reduceClipRegion(clipBounds);

    g.setColour(tapLabelBackgroundColour);
    g.fillRoundedRectangle(labelBounds.toFloat(), 3.0f);
    g.setColour(tapLabelTextColour);
    g.drawText(labelTextCstr, labelBounds, juce::Justification::centred);
}

bool TapEditItem::hitTest(int x, int y)
{
    // let the screen receive shift+click, in order to operate in draw mode
    if (juce::ModifierKeys::currentModifiers.isShiftDown())
        return false;

    return juce::Component::hitTest(x, y);
}

void TapEditItem::mouseDown(const juce::MouseEvent &e)
{
    Impl &impl = *impl_;
    juce::Rectangle<int> bounds = getLocalBounds();

    if (impl.dragChangeId_ == GDP_NONE && e.y >= bounds.getBottom() - getLabelHeight()) {
        impl.screen_->setOnlyTapSelected(impl.itemNumber_);
        impl.dragChangeId_ = GdRecomposeParameter(GDP_TAP_A_DELAY, impl.itemNumber_);
        impl.dragger_.startDraggingComponent(this, e);
        impl.listeners_.call([this](Listener &l) { l.tapEditStarted(this, impl_->dragChangeId_); });
        return;
    }

    juce::Component::mouseDown(e);
}

void TapEditItem::mouseUp(const juce::MouseEvent &e)
{
    Impl &impl = *impl_;

    if (impl.dragChangeId_ != GDP_NONE) {
        impl.listeners_.call([this](Listener &l) { l.tapEditEnded(this, impl_->dragChangeId_); });
        impl.dragChangeId_ = GDP_NONE;
        return;
    }

    juce::Component::mouseUp(e);
}

void TapEditItem::mouseDrag(const juce::MouseEvent &e)
{
    Impl &impl = *impl_;

    if (impl.dragChangeId_ != GDP_NONE) {
        class TapConstrainer : public juce::ComponentBoundsConstrainer {
        public:
            explicit TapConstrainer(TapEditScreen *screen)
                : screen_(screen)
            {
            }
            void checkBounds(juce::Rectangle<int> &bounds, const juce::Rectangle<int> &previousBounds, const juce::Rectangle<int> &, bool, bool, bool, bool) override
            {
                TapEditScreen *screen = screen_;
                float newDelay = screen->alignDelayToGrid(screen->getDelayForX(bounds.toFloat().getCentreX()));
                float halfWidth = 0.5f * (float)bounds.getWidth();
                bounds.setX(juce::roundToInt(screen->getXForDelay(newDelay) - halfWidth));
                bounds.setY(previousBounds.getY());
            }
        private:
            TapEditScreen *screen_ = nullptr;
        };
        TapEditScreen *screen = impl.screen_;
        TapConstrainer constrainer(screen);
        impl.dragger_.dragComponent(this, e, &constrainer);
        float newDelay = screen->alignDelayToGrid(screen->getDelayForX(getBounds().toFloat().getCentreX()));
        GdParameter id = GdRecomposeParameter(GDP_TAP_A_DELAY, impl.itemNumber_);
        setTapValue(id, newDelay);
        return;
    }

    juce::Component::mouseDrag(e);
}

void TapEditItem::moved()
{
    Impl &impl = *impl_;
    impl.repositionSlidersAndButtons();
}

void TapEditItem::resized()
{
    Impl &impl = *impl_;
    impl.repositionSlidersAndButtons();
    impl.updateSliderPolarities();
}

TapSlider *TapEditItem::Impl::getCurrentSlider() const
{
    return getSliderForEditMode(editMode_);
}

TapSlider *TapEditItem::Impl::getSliderForEditMode(TapEditMode editMode) const
{
    auto it = sliders_.find(editMode);
    return (it == sliders_.end()) ? nullptr : it->second.get();
}

juce::Button *TapEditItem::Impl::getCurrentButton() const
{
    return getButtonForEditMode(editMode_);
}

juce::Button *TapEditItem::Impl::getButtonForEditMode(TapEditMode editMode) const
{
    auto it = buttons_.find(editMode);
    return (it == buttons_.end()) ? nullptr : it->second.get();
}

void TapEditItem::Impl::updateSliderAndButtonVisibility()
{
    TapSlider *currentSlider = getCurrentSlider();
    for (const auto &sliderPair : sliders_) {
        TapSlider *slider = sliderPair.second.get();
        slider->setVisible(slider == currentSlider);
    }

    juce::Button *currentButton = getCurrentButton();
    for (const auto &buttonPair : buttons_) {
        juce::Button *button = buttonPair.second.get();
        button->setVisible(button == currentButton);
    }
}

void TapEditItem::Impl::updateSliderPolarities()
{
    TapEditItem *self = self_;

    if (TapSlider *slider = getSliderForEditMode(kTapEditPan)) {
        juce::AffineTransform tr;
        if (!(bool)self->getTapValue(GdRecomposeParameter(GDP_TAP_A_FLIP, itemNumber_)))
            tr = juce::AffineTransform{}.verticalFlip((float)self->getHeight());
        slider->setTransform(tr);
    }
}

void TapEditItem::Impl::repositionSlidersAndButtons()
{
    TapEditItem *self = self_;
    int labelHeight = getLabelHeight();
    int buttonHeight = getLabelHeight();

    juce::Rectangle<int> bounds = self->getLocalBounds();
    juce::Rectangle<int> sliderBounds = bounds.withTrimmedBottom(labelHeight).withTrimmedTop(buttonHeight);
    sliderBounds = sliderBounds.withSizeKeepingCentre(8, sliderBounds.getHeight());
    juce::Rectangle<int> buttonBounds = bounds.withHeight(buttonHeight).withWidth(bounds.getWidth());

    for (const auto &sliderPair : sliders_) {
        TapSlider *slider = sliderPair.second.get();
        slider->setBounds(sliderBounds);
    }

    for (const auto &buttonPair : buttons_) {
        juce::Button *button = buttonPair.second.get();
        button->setBounds(buttonBounds);
    }
}

juce::Rectangle<int> TapEditItem::Impl::getLabelBounds() const
{
    TapEditItem *self = self_;
    return self->getLocalBounds().removeFromBottom(getLabelHeight());
}

void TapEditItem::Impl::sliderValueChanged(juce::Slider *slider)
{
    juce::String identifier;
    switch (slider->getThumbBeingDragged()) {
    default:
        identifier = "X-Change-ID";
        break;
    case 1:
        identifier = "X-Change-ID-1";
        break;
    case 2:
        identifier = "X-Change-ID-2";
        break;
    }

    TapEditItem *self = self_;
    GdParameter id = (GdParameter)(int)slider->getProperties().getWithDefault(identifier, -1);
    if (id != GDP_NONE) {
        float value = self->getTapValue(id);
        listeners_.call([self, id, value](Listener &l) { l.tapValueChanged(self, id, (float)value); });
    }
}

void TapEditItem::Impl::sliderDragStarted(juce::Slider *slider)
{
    juce::String identifier;
    switch (slider->getThumbBeingDragged()) {
    default:
        identifier = "X-Change-ID";
        break;
    case 1:
        identifier = "X-Change-ID-1";
        break;
    case 2:
        identifier = "X-Change-ID-2";
        break;
    }

    TapEditItem *self = self_;
    GdParameter id = (GdParameter)(int)slider->getProperties().getWithDefault(identifier, -1);
    if (id != GDP_NONE)
        listeners_.call([self, id](Listener &l) { l.tapEditStarted(self, id); });
}

void TapEditItem::Impl::sliderDragEnded(juce::Slider *slider)
{
    juce::String identifier;
    switch (slider->getThumbBeingDragged()) {
    default:
        identifier = "X-Change-ID";
        break;
    case 1:
        identifier = "X-Change-ID-1";
        break;
    case 2:
        identifier = "X-Change-ID-2";
        break;
    }

    TapEditItem *self = self_;
    GdParameter id = (GdParameter)(int)slider->getProperties().getWithDefault(identifier, -1);
    if (id != GDP_NONE)
        listeners_.call([self, id](Listener &l) { l.tapEditEnded(self, id); });
}

void TapEditItem::Impl::buttonClicked(juce::Button *button)
{
    juce::String identifier = "X-Change-ID";

    TapEditItem *self = self_;
    GdParameter id = (GdParameter)(int)button->getProperties().getWithDefault(identifier, -1);
    if (id != GDP_NONE) {
        float value = self->getTapValue(id);
        listeners_.call([self, id, value](Listener &l) { l.tapValueChanged(self, id, (float)value); });
    }
}

TapEditItem::Impl::Slider::Slider(TapEditItem::Impl &impl)
    : impl_(impl)
{
}

void TapEditItem::Impl::Slider::paint(juce::Graphics &g)
{
    TapEditItem::Impl &impl = impl_;
    TapEditScreen *screen = impl.screen_;

    // clips painting within the screen area
    // NOTE(jpc) is there a simpler solution than reimplement `paint` just for this?
    juce::Rectangle<int> clipBounds = getLocalArea(screen, screen->getScreenArea());
    g.reduceClipRegion(clipBounds);

    TapSlider::paint(g);
}

TapEditItem::Impl::Button::Button(TapEditItem::Impl &impl)
    : juce::Button({}),
      impl_(impl)
{
    setClickingTogglesState(true);
}

void TapEditItem::Impl::Button::paintButton(juce::Graphics &g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    Impl &impl = impl_;
    TapEditScreen *screen = impl.screen_;

    // clips painting within the screen area
    juce::Rectangle<int> clipBounds = getLocalArea(screen, screen->getScreenArea());
    g.reduceClipRegion(clipBounds);

    // TODO
    (void)shouldDrawButtonAsHighlighted;

    juce::Rectangle<int> bounds = getLocalBounds().reduced(1, 1);

    juce::Colour color = TapEditScreen::getColourOfEditMode(getLookAndFeel(), impl.editMode_);
    float cornerSize = 3.0f;

    g.setColour(color);
    if (getToggleState() ^ shouldDrawButtonAsDown)
        g.fillRoundedRectangle(bounds.toFloat(), cornerSize);
    else
        g.drawRoundedRectangle(bounds.toFloat(), cornerSize, 2.0f);
}

//------------------------------------------------------------------------------
struct TapMiniMap::Impl {
    TapMiniMap *self_ = nullptr;
    juce::ListenerList<Listener> listeners_;
    juce::Range<float> timeRange_{0, GdMaxDelay};
    juce::Range<float> timeRangeBeforeMove_;
    std::vector<TapMiniMapValue> displayValues_;

    enum {
        kStatusNormal,
        kStatusMoving,
        kStatusDraggingLeft,
        kStatusDraggingRight,
    };

    enum {
        kResizeGrabMargin = 4,
    };

    int status_ = kStatusNormal;

    void updateCursor(juce::Point<float> position);
    float getXForDelay(float t) const;
    float getDelayForX(float x) const;
    juce::Rectangle<float> getRangeBounds() const;
    juce::Rectangle<float> getLeftResizeBounds() const;
    juce::Rectangle<float> getRightResizeBounds() const;
};

TapMiniMap::TapMiniMap()
    : impl_(new Impl)
{
    Impl &impl = *impl_;
    impl.self_ = this;

    setSize(200, 20);
}

TapMiniMap::~TapMiniMap()
{
}

void TapMiniMap::setTimeRange(juce::Range<float> timeRange, juce::NotificationType nt)
{
    Impl &impl = *impl_;
    if (impl.timeRange_ == timeRange)
        return;

    impl.timeRange_ = timeRange;
    repaint();

    if (nt != juce::dontSendNotification)
        impl.listeners_.call([this](Listener &l) { l.miniMapRangeChanged(this, impl_->timeRange_); });
}

void TapMiniMap::displayValues(const TapMiniMapValue values[], int count)
{
    Impl &impl = *impl_;
    impl.displayValues_.assign(values, values + count);
    repaint();
}

void TapMiniMap::addListener(Listener *listener)
{
    Impl &impl = *impl_;
    impl.listeners_.add(listener);
}

void TapMiniMap::removeListener(Listener *listener)
{
    Impl &impl = *impl_;
    impl.listeners_.remove(listener);
}

void TapMiniMap::mouseDown(const juce::MouseEvent &event)
{
    Impl &impl = *impl_;
    juce::Point<float> position = event.position;
    int status = impl.status_;

    if (status == Impl::kStatusNormal) {
        if (impl.getLeftResizeBounds().contains(position)) {
            impl.status_ = Impl::kStatusDraggingLeft;
            impl.updateCursor(event.position);
        }
        else if (impl.getRightResizeBounds().contains(position)) {
            impl.status_ = Impl::kStatusDraggingRight;
            impl.updateCursor(event.position);
        }
        else if (impl.getRangeBounds().contains(position)) {
            impl.status_ = Impl::kStatusMoving;
            impl.timeRangeBeforeMove_ = impl.timeRange_;
            impl.updateCursor(event.position);
        }
    }
}

void TapMiniMap::mouseUp(const juce::MouseEvent &event)
{
    Impl &impl = *impl_;
    juce::Point<float> position = event.position;
    int status = impl.status_;

    if (status != Impl::kStatusNormal) {
        impl.status_ = Impl::kStatusNormal;
        impl.updateCursor(position);
    }
}

void TapMiniMap::mouseMove(const juce::MouseEvent &event)
{
    Impl &impl = *impl_;
    juce::Point<float> position = event.position;
    int status = impl.status_;

    if (status == Impl::kStatusNormal)
        impl.updateCursor(position);
}

void TapMiniMap::mouseDrag(const juce::MouseEvent &event)
{
    Impl &impl = *impl_;
    juce::Point<float> position = event.position;
    int status = impl.status_;

    constexpr float kMinTimeRange = 0.2f;

    if (status == Impl::kStatusDraggingLeft) {
        juce::Rectangle<float> rangeBounds = impl.getRangeBounds();
        float minT = 0.0f;
        float maxT = std::max(minT, impl.getDelayForX(rangeBounds.getRight()) - kMinTimeRange);
        float newT = juce::jlimit(minT, maxT, impl.getDelayForX(position.getX()));
        if (impl.timeRange_.getStart() != newT) {
            impl.timeRange_.setStart(newT);
            impl.listeners_.call([this](Listener &l) { l.miniMapRangeChanged(this, impl_->timeRange_); });
            repaint();
        }
    }
    else if (status == Impl::kStatusDraggingRight) {
        juce::Rectangle<float> rangeBounds = impl.getRangeBounds();
        float maxT = GdMaxDelay;
        float minT = std::min(maxT, impl.getDelayForX(rangeBounds.getX()) + kMinTimeRange);
        float newT = juce::jlimit(minT, maxT, impl.getDelayForX(position.getX()));
        if (impl.timeRange_.getEnd() != newT) {
            impl.timeRange_.setEnd(newT);
            impl.listeners_.call([this](Listener &l) { l.miniMapRangeChanged(this, impl_->timeRange_); });
            repaint();
        }
    }
    else if (status == Impl::kStatusMoving) {
        float dt = (float)GdMaxDelay * ((position.x - (float)event.getMouseDownX()) / (float)getWidth());
        juce::Range<float> tr = impl.timeRangeBeforeMove_;
        if (dt > 0)
            dt = std::min(dt, (float)GdMaxDelay - tr.getEnd());
        else if (dt < 0)
            dt = std::max(dt, -tr.getStart());
        tr = {tr.getStart() + dt, tr.getEnd() + dt};
        if (impl.timeRange_ != tr) {
            impl.timeRange_ = tr;
            impl.listeners_.call([this](Listener &l) { l.miniMapRangeChanged(this, impl_->timeRange_); });
            repaint();
        }
    }
}

void TapMiniMap::paint(juce::Graphics &g)
{
    juce::Rectangle<int> bounds = getLocalBounds();
    juce::Rectangle<int> innerBounds = bounds.reduced(1, 1);

    juce::Colour backColour{0x40000000};
    juce::Colour rangeColour{0x60ffffff};
    juce::Colour contourColour{0x40ffffff};
    juce::Colour barColour{0x80ffffff};

    g.setColour(backColour);
    g.fillRect(bounds);
    g.setColour(contourColour);
    g.drawRect(bounds);

    g.reduceClipRegion(innerBounds);

    Impl &impl = *impl_;

    for (TapMiniMapValue mmv : impl.displayValues_) {
        float x = impl.getXForDelay(mmv.delay);
        float y1 = (float)innerBounds.getY() + (1.0f - mmv.range.getEnd()) * (float)innerBounds.getHeight();
        float y2 = (float)innerBounds.getY() + (1.0f - mmv.range.getStart()) * (float)innerBounds.getHeight();
        float barWidth = 2.0f;
        float minBarHeight = 2.0f;
        juce::Rectangle<float> barBounds = juce::Rectangle<float>::leftTopRightBottom(x - barWidth / 2, y1, x + barWidth / 2, y2);
        if (barBounds.getHeight() < minBarHeight)
            barBounds.expand(0.0f, (minBarHeight - barBounds.getHeight()) / 2.0f);
        g.setColour(barColour);
        g.fillRect(barBounds);
    }

    juce::Rectangle<float> rangeBounds = impl.getRangeBounds();
    g.setColour(rangeColour);
    g.fillRect(rangeBounds);
    g.setColour(contourColour);
    g.drawRect(rangeBounds);
}

void TapMiniMap::Impl::updateCursor(juce::Point<float> position)
{
    TapMiniMap *self = self_;

    switch (status_) {
        case kStatusNormal:
        {
            if (getLeftResizeBounds().contains(position) || getRightResizeBounds().contains(position))
                self->setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            else if (getRangeBounds().contains(position))
                self->setMouseCursor(juce::MouseCursor::PointingHandCursor);
            else
                self->setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
        }
        case kStatusMoving:
            self->setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case kStatusDraggingLeft:
        case kStatusDraggingRight:
            self->setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
    }
}

float TapMiniMap::Impl::getXForDelay(float t) const
{
    TapMiniMap *self = self_;
    juce::Rectangle<float> rc = self->getLocalBounds().reduced(1, 1).toFloat();
    return rc.getX() + rc.getWidth() * (t / (float)GdMaxDelay);
}

float TapMiniMap::Impl::getDelayForX(float x) const
{
    TapMiniMap *self = self_;
    juce::Rectangle<float> rc = self->getLocalBounds().reduced(1, 1).toFloat();
    return (float)GdMaxDelay * ((x - rc.getX()) / rc.getWidth());
}

juce::Rectangle<float> TapMiniMap::Impl::getRangeBounds() const
{
    TapMiniMap *self = self_;
    juce::Range<float> tr = timeRange_;
    return self->getLocalBounds().reduced(1, 1).toFloat()
        .withLeft(getXForDelay(tr.getStart()))
        .withRight(getXForDelay(tr.getEnd()));
}

juce::Rectangle<float> TapMiniMap::Impl::getLeftResizeBounds() const
{
    juce::Rectangle<float> rangeBounds = getRangeBounds();
    juce::Rectangle<float> boundsRszL{rangeBounds.getX(), rangeBounds.getY(), 0.0f, rangeBounds.getHeight()};
    boundsRszL.expand(kResizeGrabMargin, 0.0f);
    return boundsRszL;
}

juce::Rectangle<float> TapMiniMap::Impl::getRightResizeBounds() const
{
    juce::Rectangle<float> rangeBounds = getRangeBounds();
    juce::Rectangle<float> boundsRszR{rangeBounds.getRight(), rangeBounds.getY(), 0.0f, rangeBounds.getHeight()};
    boundsRszR.expand(kResizeGrabMargin, 0.0f);
    return boundsRszR;
}
