#include "PluginEditor.h"
#include "MidiReference.h"

namespace
{
juce::String noteName (double hz, double a4, int& centsOut)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (hz / a4);
    const int nearest = (int) std::lround (midi);
    centsOut = (int) std::lround ((midi - nearest) * 100.0);
    return juce::String (names[((nearest % 12) + 12) % 12]) + juce::String (nearest / 12 - 1);
}

juce::String midiName (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

void setupSlider (juce::Slider& s, juce::Label& l, const juce::String& text, juce::Component& parent)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    parent.addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));
    parent.addAndMakeVisible (l);
}

juce::Colour qualityColour (va::Quality q)
{
    switch (q)
    {
        case va::Quality::Green: return juce::Colour (0xff4caf50);
        case va::Quality::Amber: return juce::Colour (0xffffb300);
        case va::Quality::Red:   return juce::Colour (0xfff44336);
        default:                 return juce::Colour (0xff5c616c);
    }
}
}

VocalAssistorEditor::VocalAssistorEditor (VocalAssistorProcessor& p)
    : AudioProcessorEditor (&p), proc (p), blockEditor (p.reference, p),
      mcAttach (p.apvts, "gateMcLeod", gateMcLeod),
      yinAttach (p.apvts, "gateYin", gateYin),
      latAttach (p.apvts, "latencyMs", latency),
      octAttach (p.apvts, "octave", octave),
      offAttach (p.apvts, "refOffset", offset),
      greenAttach (p.apvts, "greenCents", greenCents),
      amberAttach (p.apvts, "amberCents", amberCents),
      a4Attach (p.apvts, "a4", a4)
{
    addAndMakeVisible (blockEditor);
    for (auto* b : { &loadButton, &bigLoadButton, &exportButton, &undoButton, &lockButton, &splitButton, &mergeButton,
                     &deleteButton, &downButton, &upButton, &fitButton, &cancelButton })
        addAndMakeVisible (*b);
    addChildComponent (progressBar);
    addAndMakeVisible (followButton);
    followButton.setToggleState (true, juce::dontSendNotification);
    followButton.onClick = [this] { blockEditor.setFollow (followButton.getToggleState()); };
    blockEditor.onFollowChanged = [this] { followButton.setToggleState (blockEditor.isFollowing(), juce::dontSendNotification); };

    loadButton.onClick = bigLoadButton.onClick = [this] { chooseReference(); };
    exportButton.onClick = [this] { exportMidi(); };
    undoButton.onClick   = [this] { proc.reference.undo(); };
    lockButton.onClick   = [this] { proc.reference.setLocked (! proc.reference.blocksCopy().isLocked()); };
    splitButton.onClick  = [this] { blockEditor.splitAtCursor(); };
    mergeButton.onClick  = [this] { blockEditor.mergeSelected(); };
    deleteButton.onClick = [this] { blockEditor.deleteSelected(); };
    downButton.onClick   = [this] { blockEditor.shiftSelected (-1); };
    upButton.onClick     = [this] { blockEditor.shiftSelected (1); };
    fitButton.onClick    = [this] { blockEditor.fitAll(); };
    cancelButton.onClick = [this] { proc.cancelAnalysis(); };
    blockEditor.onSelectionChanged = [this] { updateButtons(); };
    blockEditor.onCompare = [this] (double refHz, double sungHz, double len, const juce::String& text)
    {
        juce::String err;
        const bool viaTrack = proc.apvts.getRawParameterValue ("auditionMode")->load() < 0.5f;
        if (viaTrack)
            proc.requestComparison (refHz, sungHz, len);
        else
            err = auditioner.play (refHz, sungHz, len);
        compareText = err.isNotEmpty() ? err : text + (viaTrack ? "   (sounds on this track's output)" : "");
        compareUntilMs = juce::Time::getMillisecondCounter() + 8000;
        repaint (statusArea);
    };

    // Items must exist before the attachment is made, or nothing is selected.
    methodBox.addItemList ({ "McLeod", "YIN" }, 1);
    methodAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "method", methodBox);
    addAndMakeVisible (methodBox);
    methodLabel.setText ("Pitch method", juce::dontSendNotification);
    methodLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));
    addAndMakeVisible (methodLabel);

    setupSlider (gateMcLeod, gateMcLeodLabel, "Voicing gate, McLeod (higher = stricter)", *this);
    setupSlider (gateYin, gateYinLabel, "Voicing gate, YIN (lower = stricter)", *this);
    setupSlider (latency, latencyLabel, "Trace timing, ms (+ later, - earlier)", *this);
    setupSlider (octave, octaveLabel, "Reference octave shift", *this);
    setupSlider (offset, offsetLabel, "Reference starts at (s)", *this);
    setupSlider (greenCents, greenLabel, "Green within (cents)", *this);
    setupSlider (amberCents, amberLabel, "Amber within (cents)", *this);
    setupSlider (a4, a4Label, "A4 (Hz)", *this);
    octave.setNumDecimalPlacesToDisplay (0);
    keyBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    scaleBox.addItemList ({ "Off", "Major", "Minor" }, 1);
    keyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "key", keyBox);
    scaleAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "scale", scaleBox);
    auditionBox.addItemList ({ "This track (Cubase output)", "Windows output" }, 1);
    auditionAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "auditionMode", auditionBox);
    auditionLabel.setText ("Comparison sound plays through", juce::dontSendNotification);
    auditionLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));
    addAndMakeVisible (auditionLabel);
    for (auto* cb : { &keyBox, &scaleBox, &auditionBox }) addAndMakeVisible (*cb);
    for (auto* l : { &keyLabel, &scaleLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));
        addAndMakeVisible (*l);
    }
    keyLabel.setText ("Key", juce::dontSendNotification);
    scaleLabel.setText ("Scale (shades the rows)", juce::dontSendNotification);

    proc.reference.addChangeListener (this);
    setSize (1100, 830);
    updateButtons();
    startTimerHz (60);
}

VocalAssistorEditor::~VocalAssistorEditor() { proc.reference.removeChangeListener (this); }

void VocalAssistorEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 14);
    r.removeFromTop (34);                                  // title row, painted

    auto bar = r.removeFromTop (30);
    loadButton.setBounds (bar.removeFromLeft (170));
    bar.removeFromLeft (6);
    exportButton.setBounds (bar.removeFromLeft (110));
    bar.removeFromLeft (6);
    undoButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (6);
    lockButton.setBounds (bar.removeFromLeft (130));
    qualityArea = bar.reduced (10, 0);

    r.removeFromTop (6);
    auto edit = r.removeFromTop (28);
    for (auto* b : { &splitButton, &mergeButton, &deleteButton, &downButton, &upButton, &fitButton })
    {
        b->setBounds (edit.removeFromLeft (b == &splitButton ? 130 : 90));
        edit.removeFromLeft (6);
    }
    followButton.setBounds (edit.removeFromRight (150));

    r.removeFromTop (6);
    statusArea = r.removeFromTop (22);
    progressBar.setBounds (statusArea.withTrimmedRight (90));
    cancelButton.setBounds (statusArea.removeFromRight (80));

    auto bottom = r.removeFromBottom (200);
    r.removeFromBottom (8);
    blockEditor.setBounds (r);
    promptArea = r;
    bigLoadButton.setBounds (promptArea.withSizeKeepingCentre (280, 44).translated (0, 40));

    bigArea = bottom.removeFromLeft (360);
    readoutArea = bigArea;
    bottom.removeFromLeft (14);
    auto colA = bottom.removeFromLeft (bottom.getWidth() / 2 - 7);
    bottom.removeFromLeft (14);
    auto colB = bottom;
    auto row = [] (juce::Rectangle<int>& col, juce::Component& label, juce::Component& control)
    {
        auto line = col.removeFromTop (30);
        label.setBounds (line.removeFromLeft (190));
        control.setBounds (line);
    };
    row (colA, keyLabel, keyBox);
    row (colA, scaleLabel, scaleBox);
    row (colA, octaveLabel, octave);
    row (colA, offsetLabel, offset);
    row (colA, latencyLabel, latency);
    row (colA, auditionLabel, auditionBox);
    row (colB, greenLabel, greenCents);
    row (colB, amberLabel, amberCents);
    row (colB, a4Label, a4);
    row (colB, methodLabel, methodBox);
    row (colB, gateMcLeodLabel, gateMcLeod);
    row (colB, gateYinLabel, gateYin);
}

void VocalAssistorEditor::showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, text);
}

void VocalAssistorEditor::chooseReference()
{
    if (proc.isAnalysing())
        return;

    auto go = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose the reference vocal (MIDI is best; audio is experimental)",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.mid;*.midi;*.wav;*.flac;*.aif;*.aiff;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f.existsAsFile())
                                      startLoading (f);
                              });
    };

    if (proc.reference.hasBlocks())
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Replace the reference?")
                                          .withMessage ("This replaces the current blocks, including any edits.")
                                          .withButton ("Replace").withButton ("Cancel"),
                                      [go] (int result) { if (result == 1) go(); });
    else
        go();
}

void VocalAssistorEditor::startLoading (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".mid" || ext == ".midi")
        importMidi (f);
    else if (! proc.loadReferenceAudio (f))
        showMessage ("Busy", "An analysis is already running.");
}

void VocalAssistorEditor::importMidi (const juce::File& f)
{
    juce::FileInputStream in (f);
    juce::MidiFile midi;
    if (! in.openedOk() || ! midi.readFrom (in))
    {
        showMessage ("Not a MIDI file", "That file could not be read as MIDI.");
        return;
    }
    auto tracks = va::MidiReference::listTracks (midi);
    if (tracks.empty())
    {
        showMessage ("No notes", "That MIDI file has no notes.");
        return;
    }

    auto doImport = [this, f] (int track)
    {
        auto err = proc.loadReferenceMidi (f, track);
        if (err.isNotEmpty()) showMessage ("Could not import", err);
    };

    if (tracks.size() == 1)
    {
        doImport (tracks[0].index);
        return;
    }

    // More than one track with notes: ask which one is the lead vocal.
    juce::PopupMenu menu;
    menu.addSectionHeader ("Which track is the lead vocal?");
    for (auto& t : tracks)
        menu.addItem (t.index + 1000, "Track " + juce::String (t.index + 1) + (t.name.isNotEmpty() ? ": " + t.name : juce::String())
                                         + "   (" + juce::String (t.noteCount) + " notes, " + midiName (t.lowestNote) + " to " + midiName (t.highestNote) + ")");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton),
                        [doImport] (int choice) { if (choice >= 1000) doImport (choice - 1000); });
}

void VocalAssistorEditor::exportMidi()
{
    auto blocks = proc.reference.blocksCopy().blocks();
    if (blocks.empty()) return;
    chooser = std::make_unique<juce::FileChooser> ("Save the reference as MIDI",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("reference.mid"),
                                                   "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, blocks] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension ("mid");
                              f.deleteFile();
                              juce::FileOutputStream out (f);
                              if (! out.openedOk() || ! va::MidiReference::exportBlocks (blocks).writeTo (out))
                                  showMessage ("Could not save", "The MIDI file could not be written.");
                          });
}

void VocalAssistorEditor::updateButtons()
{
    const auto list = proc.reference.blocksCopy();
    const bool has = ! list.empty(), busy = proc.isAnalysing(), locked = list.isLocked();
    const int sel = blockEditor.selectedCount();

    loadButton.setEnabled (! busy);
    bigLoadButton.setVisible (! has && ! busy);
    blockEditor.setVisible (has || busy);
    repaint();
    exportButton.setEnabled (has);
    undoButton.setEnabled (proc.reference.canUndo() && ! locked);
    lockButton.setEnabled (has);
    lockButton.setButtonText (locked ? "Unlock reference" : "Lock reference");
    splitButton.setEnabled (! locked && sel == 1 && blockEditor.hasCursor());
    mergeButton.setEnabled (! locked && sel >= 1);
    deleteButton.setEnabled (! locked && sel >= 1);
    downButton.setEnabled (! locked && sel >= 1);
    upButton.setEnabled (! locked && sel >= 1);
    fitButton.setEnabled (has);
    cancelButton.setVisible (busy);
    progressBar.setVisible (busy);
}

void VocalAssistorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateButtons();
    auto err = proc.takeAnalysisError();
    if (err.isNotEmpty())
        showMessage ("Analysis failed", err);
    repaint();
}

void VocalAssistorEditor::timerCallback()
{
    progressValue = (double) proc.getAnalysisProgress();
    if (proc.isAnalysing() != progressBar.isVisible())
        updateButtons();
    if (proc.peekAnalysisError().isNotEmpty())
        changeListenerCallback (nullptr);

    haveLatest = proc.getLatestPoint (latest);
    if (compareText.isNotEmpty() && juce::Time::getMillisecondCounter() >= compareUntilMs)
    {
        compareText.clear();
        repaint (statusArea);
    }
    blockEditor.tick();
    repaint (bigArea);
}

void VocalAssistorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Vocal Assistor", 20, 12, 220, 30, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff7fb2ff));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (juce::String ("Version ") + VA_VERSION_LABEL + "",
                240, 12, 600, 30, juce::Justification::centredLeft);

    // Quality indicator.
    const auto info = proc.reference.infoCopy();
    if (info.quality != va::Quality::None)
    {
        auto q = qualityArea;
        g.setColour (qualityColour (info.quality));
        g.fillEllipse ((float) q.getX(), (float) q.getCentreY() - 8.0f, 16.0f, 16.0f);
        g.setColour (juce::Colour (0xffd5d8df));
        g.setFont (juce::FontOptions (13.0f));
        juce::String why;
        if (info.kind == va::SourceKind::Midi)
            why = "MIDI, exact";
        else if (info.quality == va::Quality::Green)
            why = "clean monophonic audio (experimental)";
        else if (info.quality == va::Quality::Amber)
            why = "processed but usable (experimental)";
        else
            why = "heavy reverb or doubles, blocks unreliable (experimental)";
        g.drawText ("Analysis quality: " + juce::String (va::qualityName (info.quality)) + " - " + why,
                    q.withTrimmedLeft (24), juce::Justification::centredLeft);
    }

    // Status line.
    g.setColour (juce::Colour (0xff9aa0ab));
    g.setFont (juce::FontOptions (13.0f));
    if (compareText.isNotEmpty() && juce::Time::getMillisecondCounter() < compareUntilMs)
    {
        g.setColour (juce::Colour (0xff00e5ff));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (compareText, statusArea, juce::Justification::centredLeft);
    }
    else if (proc.isAnalysing())
        g.drawText ("Analysing " + proc.getAnalysisFileName() + " (experimental audio analysis)...", statusArea.withTrimmedRight (200), juce::Justification::centredLeft);
    else if (info.kind != va::SourceKind::None)
    {
        const auto list = proc.reference.blocksCopy();
        g.drawText ("Reference: " + info.sourceName + "   " + juce::String ((int) list.blocks().size()) + " blocks   "
                        + (list.isLocked() ? "locked" : "unlocked, editable")
                        + "   Click a block or one of your notes (cyan) to hear reference then you.",
                    statusArea, juce::Justification::centredLeft);
    }

    // First-open prompt, shown in place of the block view while there is no reference.
    if (! proc.reference.hasBlocks() && ! proc.isAnalysing())
    {
        auto area = promptArea;
        g.setColour (juce::Colour (0xff101216));
        g.fillRoundedRectangle (area.toFloat(), 8.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText ("Add your reference vocal", area.withSizeKeepingCentre (620, 34).translated (0, -90), juce::Justification::centred);
        g.setColour (juce::Colour (0xffb8bcc6));
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("Choose a MIDI file of the lead vocal (best), or an audio stem (WAV, FLAC, AIFF, OGG).\n"
                          "Audio analysis is experimental. Either way you can then fix the note blocks and lock them.",
                          area.withSizeKeepingCentre (620, 70).translated (0, -34), juce::Justification::centred, 3);
    }

    // Big readout: ON only while the singer is inside the green threshold.
    g.setColour (juce::Colour (0xff1f232b));
    g.fillRoundedRectangle (bigArea.toFloat(), 8.0f);
    {
        auto box = bigArea.reduced (14, 8);
        const auto& m = proc.matcher();
        const auto& live = m.live();
        const bool active = proc.isPlaying() && live.hasBlock;

        juce::String big = "--", hint = proc.isPlaying() ? "no reference block here" : "press play";
        juce::Colour colour (0xff6c727f);
        if (active)
        {
            const bool on = live.instant == va::BlockState::Green;
            big = on ? "ON" : "OFF";
            colour = on ? juce::Colour (0xff4caf50) : (live.instant == va::BlockState::Amber ? juce::Colour (0xffffb300) : juce::Colour (0xfff44336));
            if (! live.voiced)
                hint = "no pitch detected";
            else
            {
                const int c = (int) std::lround (std::abs (live.cents));
                hint = on ? juce::String (c) + " cents " + (live.cents < 0 ? "flat, in tune" : "sharp, in tune")
                          : juce::String (c) + " cents " + (live.cents < 0 ? "LOW - sing higher" : "HIGH - sing lower");
            }
        }
        g.setColour (colour);
        g.setFont (juce::FontOptions (72.0f, juce::Font::bold));
        g.drawText (big, box.removeFromTop (84), juce::Justification::centred);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (hint, box.removeFromTop (28), juce::Justification::centred);

        g.setColour (juce::Colour (0xffd5d8df));
        g.setFont (juce::FontOptions (18.0f));
        g.drawText (m.hasAccuracy() ? "Accuracy " + juce::String (m.accuracyPercent()) + "%" : juce::String ("Accuracy --"),
                    box.removeFromTop (28), juce::Justification::centred);

        g.setColour (juce::Colour (0xff8a8f99));
        g.setFont (juce::FontOptions (12.0f));
        juce::String line = "Waiting for audio";
        if (haveLatest && latest.voiced)
        {
            int cents = 0;
            const auto name = noteName (latest.hz, proc.viewSettings().a4, cents);
            line = "Singing " + name + (cents >= 0 ? " +" : " ") + juce::String (cents) + " cents  (" + juce::String (latest.hz, 1) + " Hz)";
        }
        else if (haveLatest)
            line = "Singing: no pitch";
        g.drawText (line, box.removeFromTop (18), juce::Justification::centred);
        g.drawText (juce::String ("Delay ") + juce::String (va::PitchEngine::algorithmicDelayMs (proc.engine.getSampleRate()), 1)
                        + " ms, dropped " + juce::String (proc.engine.getDroppedSamples()),
                    box, juce::Justification::centred);
    }
}
