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

#include "processor/Processor.h"
#include "processor/PresetFile.h"
#include "editor/Editor.h"
#include "GdJuce.h"
#include "Gd.h"
#include <algorithm>
#include <mutex>

struct Processor::Impl : public juce::AudioProcessorListener {
    explicit Impl(Processor *self);
    void setupParameters();

    //==========================================================================
    void updateBPM(double newBpm);

    //==========================================================================
    void audioProcessorParameterChanged(AudioProcessor *processor, int parameterIndex, float newValue) override;
    void audioProcessorChanged(AudioProcessor *processor, const ChangeDetails &details) override;

    //==========================================================================
    Processor *self_ = nullptr;
    GdPtr gd_;
    double lastKnownBpm_ = -1.0;

    //==========================================================================
    using NameBuffer = PresetFile::NameBuffer;
    NameBuffer presetNameBuf_{};
    mutable std::mutex presetNameMutex_;

    //==========================================================================
    class EditorStateUpdater : public juce::AsyncUpdater {
    public:
        explicit EditorStateUpdater(Impl &impl);
        void handleAsyncUpdate() override;
    private:
        Impl &impl_;
    };
    EditorStateUpdater editorStateUpdater_;
};

//==============================================================================
Processor::Processor()
    : juce::AudioProcessor(BusesProperties()
                           .withInput("Input", juce::AudioChannelSet::stereo(), true)
                           .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      impl_(new Impl(this))
{
    Impl &impl = *impl_;
    addListener(&impl);
    impl.setupParameters();
}

Processor::~Processor()
{
    Impl &impl = *impl_;
    removeListener(&impl);
}

//==============================================================================
double Processor::getLastKnownBPM() const
{
    Impl &impl = *impl_;
    double bpm = impl.lastKnownBpm_;
    return (bpm != -1.0) ? bpm : 120.0;
}

void Processor::setCurrentPresetName(const juce::String &newName)
{
    Impl &impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.presetNameMutex_);
    impl.presetNameBuf_ = PresetFile::nameFromString(newName);
}

juce::String Processor::getCurrentPresetName() const
{
    Impl &impl = *impl_;
    std::unique_lock<std::mutex> lock(impl.presetNameMutex_);
    Impl::NameBuffer nameBuf = impl.presetNameBuf_;
    lock.unlock();
    return PresetFile::nameToString(nameBuf);
}

//==============================================================================
void Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    Impl &impl = *impl_;
    Gd *gd = impl.gd_.get();

    if (!gd) {
        BusesLayout layouts = getBusesLayout();
        juce::AudioChannelSet inputs = layouts.getMainInputChannelSet();
        int numInputs = (inputs == juce::AudioChannelSet::stereo()) ? 2 : 1;
        int numOutputs = 2;
        gd = GdNew((unsigned)numInputs, (unsigned)numOutputs);
        jassert(gd);
        impl.gd_.reset(gd);
    }

    GdSetSampleRate(gd, (float)sampleRate);
    GdSetBufferSize(gd, (unsigned)samplesPerBlock);
    GdSetTempo(gd, 120.0f);

    for (unsigned i = 0; i < GD_PARAMETER_COUNT; ++i) {
        const auto &parameter = static_cast<const juce::RangedAudioParameter &>(*getParameters()[(int)i]);
        float value = parameter.convertFrom0to1(parameter.getValue());
        GdSetParameter(gd, (GdParameter)i, value);
    }

    GdClear(gd);

    impl.lastKnownBpm_ = -1.0;
}

void Processor::releaseResources()
{
    Impl &impl = *impl_;
    impl.gd_.reset();
}

bool Processor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    juce::AudioChannelSet inputs = layouts.getMainInputChannelSet();
    juce::AudioChannelSet outputs = layouts.getMainOutputChannelSet();

    return (inputs == juce::AudioChannelSet::mono() ||
            inputs == juce::AudioChannelSet::stereo()) &&
        outputs == juce::AudioChannelSet::stereo();
}

bool Processor::applyBusLayouts(const BusesLayout& layouts)
{
    if (layouts == getBusesLayout())
        return true;

    if (!AudioProcessor::applyBusLayouts(layouts))
        return false;

    Impl &impl = *impl_;
    if (impl.gd_) {
        impl.gd_.reset();
        prepareToPlay(getSampleRate(), getBlockSize());
    }

    return true;
}

void Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    (void)midiMessages;

    Impl &impl = *impl_;

    ///
    juce::AudioPlayHead *playHead = getPlayHead();
    juce::Optional<juce::AudioPlayHead::PositionInfo> positionInfo = playHead->getPosition();
    if (juce::Optional<double> bpm = positionInfo->getBpm())
        impl.updateBPM(*bpm);

    ///
    Gd *gd = impl.gd_.get();
    const float **inputs = buffer.getArrayOfReadPointers();
    float **outputs = buffer.getArrayOfWritePointers();
    GdProcess(gd, inputs, outputs, (unsigned)buffer.getNumSamples());
}

void Processor::processBlock(juce::AudioBuffer<double> &buffer, juce::MidiBuffer &midiMessages)
{
    (void)buffer;
    (void)midiMessages;
    jassertfalse;
}

//==============================================================================
juce::AudioProcessorEditor *Processor::createEditor()
{
    Editor *editor = new Editor(*this);
    return editor;
}

bool Processor::hasEditor() const
{
    return true;
}

//==============================================================================
const juce::String Processor::getName() const
{
    return JucePlugin_Name;
}

bool Processor::acceptsMidi() const
{
    return false;
}

bool Processor::producesMidi() const
{
    return false;
}

bool Processor::isMidiEffect() const
{
    return false;
}

double Processor::getTailLengthSeconds() const
{
    return (double)GdMaxDelay;
}

//==============================================================================
int Processor::getNumPrograms()
{
    return 1;
}

int Processor::getCurrentProgram()
{
    return 0;
}

void Processor::setCurrentProgram(int index)
{
    (void)index;
}

const juce::String Processor::getProgramName(int index)
{
    (void)index;
    return {};
}

void Processor::changeProgramName(int index, const juce::String &newName)
{
    (void)index;
    (void)newName;
}

//==============================================================================
void Processor::getStateInformation(juce::MemoryBlock &destData)
{
    PresetFile pst;
    pst.valid = true;

    const Impl &impl = *impl_;
    std::unique_lock<std::mutex> lock(impl.presetNameMutex_);
    pst.name = impl.presetNameBuf_;
    lock.unlock();

    for (unsigned i = 0; i < GD_PARAMETER_COUNT; ++i) {
        const auto &parameter = static_cast<const juce::RangedAudioParameter &>(*getParameters()[(int)i]);
        pst.values[i] = parameter.convertFrom0to1(parameter.getValue());
    }

    bool saveOk = PresetFile::saveToData(pst, destData);
    jassert(saveOk);
    (void)saveOk;
}

void Processor::setStateInformation(const void *data, int sizeInBytes)
{
    PresetFile pst = PresetFile::loadFromData(data, (size_t)sizeInBytes);
    if (!pst)
        pst = PresetFile::makeDefault();

    Impl &impl = *impl_;
    std::unique_lock<std::mutex> lock(impl.presetNameMutex_);
    impl.presetNameBuf_ = pst.name;
    lock.unlock();

    for (unsigned i = 0; i < GD_PARAMETER_COUNT; ++i) {
        auto &parameter = static_cast<juce::RangedAudioParameter &>(*getParameters()[(int)i]);
        parameter.setValueNotifyingHost(parameter.convertTo0to1(pst.values[i]));
    }

    impl.editorStateUpdater_.triggerAsyncUpdate();
}

//==============================================================================
Processor::Impl::Impl(Processor *self)
    : self_(self),
      editorStateUpdater_(*this)
{
}

void Processor::Impl::setupParameters()
{
    Processor *self = self_;

    std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>> parameterGroups;
    juce::AudioProcessorParameterGroup *lastPg = nullptr;

    parameterGroups.reserve(32);

    for (unsigned i = 0; i < GD_PARAMETER_COUNT; ++i) {
        unsigned flags = GdParameterFlags((GdParameter)i);
        const char *name = GdParameterName((GdParameter)i);
        const char *label = GdParameterLabel((GdParameter)i);
        GdRange range = GdParameterRange((GdParameter)i);
        float def = GdParameterDefault((GdParameter)i);
        int group = GdParameterGroup((GdParameter)i);
        unsigned type = flags & (GDP_FLOAT|GDP_BOOLEAN|GDP_INTEGER|GDP_CHOICE);

        ///
        int previousGroup = (int)parameterGroups.size() - 1;
        if (group != previousGroup) {
            jassert(group == previousGroup + 1);
            const char *groupName = GdGroupName((GdParameter)i);
            const char *groupLabel = GdGroupLabel((GdParameter)i);
            lastPg = new juce::AudioProcessorParameterGroup(groupName, groupLabel, "|");
            parameterGroups.emplace_back(lastPg);
        }

        ///
        juce::AudioProcessorParameter *parameter;

        auto stringFromValue = [i](float value, int) -> juce::String {
            char text[256];
            GdFormatParameterValue((GdParameter)i, value, text, sizeof(text));
            return text;
        };

        switch (type) {
        default:
        case GDP_FLOAT:
        {
            parameter = new juce::AudioParameterFloat(
                name, label, GdJuceRange<float>(range), def, juce::AudioParameterFloatAttributes{}
                .withStringFromValueFunction(stringFromValue));
            break;
        }
        case GDP_BOOLEAN:
            parameter = new juce::AudioParameterBool(
                name, label, (bool)def, juce::AudioParameterBoolAttributes{}
                .withStringFromValueFunction(stringFromValue));
            break;
        case GDP_INTEGER:
        {
            parameter = new juce::AudioParameterInt(
                name, label, (int)range.start, (int)range.end, (int)def, juce::AudioParameterIntAttributes{}
                .withStringFromValueFunction(stringFromValue));
            break;
        }
        case GDP_CHOICE:
            {
                juce::StringArray choices;
                choices.ensureStorageAllocated(32);
                for (const char *const *p = GdParameterChoices((GdParameter)i); *p; ++p)
                    choices.add(*p);
                parameter = new juce::AudioParameterChoice(
                    name, label, std::move(choices), (int)def, juce::AudioParameterChoiceAttributes{}
                    .withStringFromValueFunction(stringFromValue));
            }
            break;
        }

        if (!lastPg)
            self->addParameter(parameter);
        else
            lastPg->addChild(std::unique_ptr<juce::AudioProcessorParameter>(parameter));
    }

    for (int i = 0, n = (int)parameterGroups.size(); i < n; ++i) {
        self->addParameterGroup(std::move(parameterGroups[(size_t)i]));
    }
}

//==============================================================================
void Processor::Impl::updateBPM(double newBpm)
{
    Processor *self = self_;

    double oldBpm = lastKnownBpm_;
    if (oldBpm == newBpm)
        return;
    lastKnownBpm_ = newBpm;

    Gd *gd = gd_.get();
    if (gd)
        GdSetTempo(gd, (float)newBpm);

    if (oldBpm != -1.0) {
        float scaleRatio = (float)(oldBpm / newBpm);
        juce::AudioParameterBool *sync = static_cast<juce::AudioParameterBool *>(self->getParameters()[(int)GDP_SYNC]);
        if (*sync) {
            for (int tapNumber = 0; tapNumber < GdMaxLines; ++tapNumber) {
                GdParameter delayId = GdRecomposeParameter(GDP_TAP_A_DELAY, tapNumber);
                juce::AudioParameterFloat *delay = static_cast<juce::AudioParameterFloat *>(self->getParameters()[(int)delayId]);
                *delay = scaleRatio * *delay;
            }
        }
    }
}

//==============================================================================
void Processor::Impl::audioProcessorParameterChanged(AudioProcessor *processor, int parameterIndex, float newValue)
{
    (void)processor;

    Processor *self = self_;
    Gd *gd = gd_.get();

    if (gd) {
        const auto &parameter = static_cast<const juce::RangedAudioParameter &>(*self->getParameters()[(int)parameterIndex]);
        newValue = parameter.convertFrom0to1(newValue);
        GdSetParameter(gd, (GdParameter)parameterIndex, newValue);
    }
}

void Processor::Impl::audioProcessorChanged(AudioProcessor *processor, const ChangeDetails &details)
{
    (void)processor;
    (void)details;
}

//==============================================================================
Processor::Impl::EditorStateUpdater::EditorStateUpdater(Impl &impl)
    : impl_(impl)
{
}

void Processor::Impl::EditorStateUpdater::handleAsyncUpdate()
{
    Impl &impl = impl_;
    Processor *self = impl.self_;

    if (Editor *editor = static_cast<Editor *>(self->getActiveEditor()))
        editor->syncStateFromProcessor();
}

//==============================================================================
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new Processor;
}
