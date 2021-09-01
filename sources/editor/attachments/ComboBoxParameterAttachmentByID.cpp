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

#include "ComboBoxParameterAttachmentByID.h"

ComboBoxParameterAttachmentByID::ComboBoxParameterAttachmentByID(juce::RangedAudioParameter &param, juce::ComboBox &c, juce::UndoManager *um)
    : comboBox_(c),
      attachment_(param, [this](float f) { setValue (f); }, um)
{
    sendInitialUpdate();
    comboBox_.addListener(this);
}

ComboBoxParameterAttachmentByID::~ComboBoxParameterAttachmentByID()
{
    comboBox_.removeListener (this);
}

void ComboBoxParameterAttachmentByID::sendInitialUpdate()
{
    attachment_.sendInitialUpdate();
}

void ComboBoxParameterAttachmentByID::setValue(float newValue)
{
    int id = 1 + (int)newValue;

    if (id == comboBox_.getSelectedId())
        return;

    const juce::ScopedValueSetter<bool> svs(ignoreCallbacks_, true);
    comboBox_.setSelectedId(id, juce::sendNotificationSync);
}

void ComboBoxParameterAttachmentByID::comboBoxChanged(juce::ComboBox *)
{
    if (ignoreCallbacks_)
        return;

    float selected = (float)(comboBox_.getSelectedId() - 1);
    attachment_.setValueAsCompleteGesture(selected);
}