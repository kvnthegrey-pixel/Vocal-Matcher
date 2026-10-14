#include "AssistEditor.h"
#include "../MidiReference.h"

namespace
{
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

juce::String clock (double sec)
{
    sec = std::max (0.0, sec);
    const int m = (int) (sec / 60.0);
    return juce::String (m) + ":" + juce::String (sec - m * 60.0, 3).paddedLeft ('0', 6);
}
}

AssistEditor::AssistEditor (AssistProcessor& p)
    : AudioProcessorEditor (&p), proc (p), gameView (p), takesView (p), blockEditor (p.reference, p),
      octAttach (p.apvts, "octave", octave),
      offAttach (p.apvts, "refOffset", offset),
      latAttach (p.apvts, "latencyMs", latency),
      perfAttach (p.apvts, "perfectCents", perfectCents),
      goodAttach (p.apvts, "goodCents", goodCents),
      graceAttach (p.apvts, "graceMs", graceMs),
      smoothAttach (p.apvts, "smoothMs", smoothMs),
      gainAttach (p.apvts, "waveGain", waveGain),
      windowAttach (p.apvts, "windowSec", windowSec),
      a4Attach (p.apvts, "a4", a4),
      mcAttach (p.apvts, "gateMcLeod", gateMcLeod),
      yinAttach (p.apvts, "gateYin", gateYin)
{
    addAndMakeVisible (gameView);
    addChildComponent (takesView);
    addChildComponent (blockEditor);
    addChildComponent (progressBar);

    for (auto* b : { &tabPerform, &tabTakes, &tabEdit, &tabSettings })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (91);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3f6fd8));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (*b);
    }
    tabPerform.setToggleState (true, juce::dontSendNotification);
    tabPerform.onClick  = [this] { showTab (Tab::Perform); };
    tabTakes.onClick    = [this] { showTab (Tab::Takes); };
    tabEdit.onClick     = [this] { showTab (Tab::Edit); };
    tabSettings.onClick = [this] { showTab (Tab::Settings); };

    for (auto* b : { &loadButton, &bigLoadButton, &cancelButton, &copyCycleButton })
        addAndMakeVisible (*b);
    loadButton.onClick = bigLoadButton.onClick = [this] { chooseReference(); };
    cancelButton.onClick = [this] { proc.cancelAnalysis(); };
    copyCycleButton.onClick = [this]
    {
        const auto cfg = proc.score().settings();
        double a = 0, b = 0;
        if (proc.score().phraseRange (cfg.focusPhrase, a, b))
            juce::SystemClipboard::copyTextToClipboard (clock (a) + " - " + clock (b) + "   (" + juce::String (a, 3) + " s to " + juce::String (b, 3) + " s)");
    };

    // Mode and focus live in the top bar, so they are one click away while you practise.
    modeBox.addItemList ({ "Play", "Ghost (sing from memory)" }, 1);
    modeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "mode", modeBox);
    addAndMakeVisible (modeBox);
    modeLabel.setText ("Mode", juce::dontSendNotification);
    focusLabel.setText ("Practise", juce::dontSendNotification);
    for (auto* l : { &modeLabel, &focusLabel, &cycleLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));
        addAndMakeVisible (*l);
    }
    cycleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffb300));
    addAndMakeVisible (focusBox);
    focusBox.onChange = [this]
    {
        if (auto* focus = dynamic_cast<juce::AudioParameterInt*> (proc.apvts.getParameter ("focus")))
            *focus = std::max (0, focusBox.getSelectedId() - 1);
        updateCycleLabel();
    };

    // Edit tab
    for (auto* b : { &undoButton, &lockButton, &splitButton, &mergeButton, &deleteButton, &downButton, &upButton, &fitButton })
        addChildComponent (*b);
    addChildComponent (followButton);
    followButton.setToggleState (true, juce::dontSendNotification);
    followButton.onClick = [this] { blockEditor.setFollow (followButton.getToggleState()); };
    blockEditor.onFollowChanged = [this] { followButton.setToggleState (blockEditor.isFollowing(), juce::dontSendNotification); };
    undoButton.onClick   = [this] { proc.reference.undo(); };
    lockButton.onClick   = [this] { proc.reference.setLocked (! proc.reference.blocksCopy().isLocked()); };
    splitButton.onClick  = [this] { blockEditor.splitAtCursor(); };
    mergeButton.onClick  = [this] { blockEditor.mergeSelected(); };
    deleteButton.onClick = [this] { blockEditor.deleteSelected(); };
    downButton.onClick   = [this] { blockEditor.shiftSelected (-1); };
    upButton.onClick     = [this] { blockEditor.shiftSelected (1); };
    fitButton.onClick    = [this] { blockEditor.fitAll(); };
    blockEditor.onSelectionChanged = [this] { updateButtons(); };

    // Settings tab
    setupSlider (perfectCents, perfectLabel, "Perfect within (cents)", *this);
    setupSlider (goodCents, goodLabel, "Good within (cents)", *this);
    setupSlider (graceMs, graceLabel, "Note start forgiveness (ms)", *this);
    setupSlider (smoothMs, smoothLabel, "Vibrato smoothing (ms)", *this);
    setupSlider (octave, octaveLabel, "Reference octave shift", *this);
    setupSlider (offset, offsetLabel, "Reference starts at (s)", *this);
    setupSlider (latency, latencyLabel, "Voice timing, ms (+ later, - earlier)", *this);
    setupSlider (waveGain, gainLabel, "Voice wave size", *this);
    setupSlider (windowSec, windowLabel, "Seconds on screen", *this);
    setupSlider (a4, a4Label, "A4 (Hz)", *this);
    setupSlider (gateMcLeod, gateMcLeodLabel, "Voicing gate, McLeod (higher = stricter)", *this);
    setupSlider (gateYin, gateYinLabel, "Voicing gate, YIN (lower = stricter)", *this);
    octave.setNumDecimalPlacesToDisplay (0);
    for (auto* s : { &perfectCents, &goodCents, &graceMs, &smoothMs, &octave, &offset, &latency, &waveGain, &windowSec, &a4, &gateMcLeod, &gateYin })
        s->setVisible (false);
    for (auto* l : { &perfectLabel, &goodLabel, &graceLabel, &smoothLabel, &octaveLabel, &offsetLabel, &latencyLabel, &gainLabel,
                     &windowLabel, &a4Label, &gateMcLeodLabel, &gateYinLabel })
        l->setVisible (false);

    keyBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    scaleBox.addItemList ({ "Off", "Major", "Minor" }, 1);
    methodBox.addItemList ({ "McLeod", "YIN" }, 1);
    keyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "key", keyBox);
    scaleAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "scale", scaleBox);
    methodAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "method", methodBox);
    keyLabel.setText ("Key", juce::dontSendNotification);
    scaleLabel.setText ("Scale (shades the rows)", juce::dontSendNotification);
    methodLabel.setText ("Pitch method", juce::dontSendNotification);
    settingsNote.setText ("Perfect and Good are how close your pitch must be to the note. Start forgiveness ignores the scoop into each note. "
                          "Vibrato smoothing averages your pitch over about one vibrato cycle so natural vibrato is not marked wrong.",
                          juce::dontSendNotification);
    settingsNote.setJustificationType (juce::Justification::topLeft);
    for (auto* c : { (juce::Component*) &keyBox, (juce::Component*) &scaleBox, (juce::Component*) &methodBox,
                     (juce::Component*) &keyLabel, (juce::Component*) &scaleLabel, (juce::Component*) &methodLabel,
                     (juce::Component*) &settingsNote })
    {
        addChildComponent (*c);
    }
    for (auto* l : { &keyLabel, &scaleLabel, &methodLabel, &settingsNote })
        l->setColour (juce::Label::textColourId, juce::Colour (0xffb8bcc6));

    proc.reference.addChangeListener (this);
    setSize (1180, 780);
    syncFocusBox();
    updateButtons();
    showTab (Tab::Perform);
    startTimerHz (60);
}

AssistEditor::~AssistEditor() { proc.reference.removeChangeListener (this); }

void AssistEditor::showTab (Tab t)
{
    tab = t;
    const bool perform = t == Tab::Perform, takes = t == Tab::Takes, edit = t == Tab::Edit, settings = t == Tab::Settings;
    tabPerform.setToggleState (perform, juce::dontSendNotification);
    tabTakes.setToggleState (takes, juce::dontSendNotification);
    tabEdit.setToggleState (edit, juce::dontSendNotification);
    tabSettings.setToggleState (settings, juce::dontSendNotification);

    gameView.setVisible (perform);
    takesView.setVisible (takes);
    for (auto* b : { &undoButton, &lockButton, &splitButton, &mergeButton, &deleteButton, &downButton, &upButton, &fitButton })
        b->setVisible (edit);
    followButton.setVisible (edit);
    for (auto* c : { (juce::Component*) &keyBox, (juce::Component*) &scaleBox, (juce::Component*) &methodBox,
                     (juce::Component*) &keyLabel, (juce::Component*) &scaleLabel, (juce::Component*) &methodLabel,
                     (juce::Component*) &settingsNote, (juce::Component*) &perfectCents, (juce::Component*) &goodCents,
                     (juce::Component*) &graceMs, (juce::Component*) &smoothMs, (juce::Component*) &octave, (juce::Component*) &offset,
                     (juce::Component*) &latency, (juce::Component*) &waveGain, (juce::Component*) &windowSec, (juce::Component*) &a4,
                     (juce::Component*) &gateMcLeod, (juce::Component*) &gateYin,
                     (juce::Component*) &perfectLabel, (juce::Component*) &goodLabel, (juce::Component*) &graceLabel,
                     (juce::Component*) &smoothLabel, (juce::Component*) &octaveLabel, (juce::Component*) &offsetLabel,
                     (juce::Component*) &latencyLabel, (juce::Component*) &gainLabel, (juce::Component*) &windowLabel,
                     (juce::Component*) &a4Label, (juce::Component*) &gateMcLeodLabel, (juce::Component*) &gateYinLabel })
        c->setVisible (settings);
    updateButtons();
    resized();
    repaint();
}

void AssistEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 14);
    r.removeFromTop (34);                                   // title row, painted

    auto bar = r.removeFromTop (30);
    loadButton.setBounds (bar.removeFromLeft (170));
    bar.removeFromLeft (14);
    modeLabel.setBounds (bar.removeFromLeft (40));
    modeBox.setBounds (bar.removeFromLeft (190));
    bar.removeFromLeft (14);
    focusLabel.setBounds (bar.removeFromLeft (56));
    focusBox.setBounds (bar.removeFromLeft (140));
    tabSettings.setBounds (bar.removeFromRight (90));
    bar.removeFromRight (4);
    tabEdit.setBounds (bar.removeFromRight (120));
    bar.removeFromRight (4);
    tabTakes.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (4);
    tabPerform.setBounds (bar.removeFromRight (90));

    r.removeFromTop (6);
    statusArea = r.removeFromTop (22);
    progressBar.setBounds (statusArea.withTrimmedRight (90));
    cancelButton.setBounds (statusArea.removeFromRight (80));
    {
        auto cyc = statusArea;
        copyCycleButton.setBounds (cyc.removeFromRight (130));
        cyc.removeFromRight (8);
        cycleLabel.setBounds (cyc.removeFromRight (360));
    }
    r.removeFromTop (4);

    if (tab == Tab::Edit)
    {
        auto edit = r.removeFromTop (28);
        for (auto* b : { &undoButton, &lockButton, &splitButton, &mergeButton, &deleteButton, &downButton, &upButton, &fitButton })
        {
            b->setBounds (edit.removeFromLeft (b == &splitButton || b == &lockButton ? 130 : 90));
            edit.removeFromLeft (6);
        }
        followButton.setBounds (edit.removeFromRight (150));
        r.removeFromTop (6);
    }

    contentArea = r;
    gameView.setBounds (r);
    takesView.setBounds (r);
    blockEditor.setBounds (r);
    bigLoadButton.setBounds (r.withSizeKeepingCentre (280, 44).translated (0, 50));

    if (tab == Tab::Settings)
    {
        auto area = r.reduced (10, 10);
        auto colA = area.removeFromLeft (area.getWidth() / 2 - 14);
        area.removeFromLeft (28);
        auto colB = area;
        auto row = [] (juce::Rectangle<int>& col, juce::Component& label, juce::Component& control)
        {
            auto line = col.removeFromTop (32);
            label.setBounds (line.removeFromLeft (230));
            control.setBounds (line);
        };
        row (colA, perfectLabel, perfectCents);
        row (colA, goodLabel, goodCents);
        row (colA, graceLabel, graceMs);
        row (colA, smoothLabel, smoothMs);
        row (colA, octaveLabel, octave);
        row (colA, offsetLabel, offset);
        row (colB, keyLabel, keyBox);
        row (colB, scaleLabel, scaleBox);
        row (colB, latencyLabel, latency);
        row (colB, gainLabel, waveGain);
        row (colB, windowLabel, windowSec);
        row (colB, a4Label, a4);
        colA.removeFromTop (10);
        settingsNote.setBounds (colA.removeFromTop (90));
        colB.removeFromTop (10);
        row (colB, methodLabel, methodBox);
        row (colB, gateMcLeodLabel, gateMcLeod);
        row (colB, gateYinLabel, gateYin);
    }
}

void AssistEditor::showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, text);
}

void AssistEditor::chooseReference()
{
    if (proc.isAnalysing())
        return;

    auto go = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose the reference vocal (MIDI is exact; audio is analysed into notes)",
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
                                          .withMessage ("This replaces the current blocks, including any edits. Your takes are kept.")
                                          .withButton ("Replace").withButton ("Cancel"),
                                      [go] (int result) { if (result == 1) go(); });
    else
        go();
}

void AssistEditor::startLoading (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".mid" || ext == ".midi")
        importMidi (f);
    else if (! proc.loadReferenceAudio (f))
        showMessage ("Busy", "An analysis is already running.");
}

void AssistEditor::importMidi (const juce::File& f)
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

    juce::PopupMenu menu;
    menu.addSectionHeader ("Which track is the lead vocal?");
    for (auto& t : tracks)
        menu.addItem (t.index + 1000, "Track " + juce::String (t.index + 1) + (t.name.isNotEmpty() ? ": " + t.name : juce::String())
                                         + "   (" + juce::String (t.noteCount) + " notes, " + midiName (t.lowestNote) + " to " + midiName (t.highestNote) + ")");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton),
                        [doImport] (int choice) { if (choice >= 1000) doImport (choice - 1000); });
}

void AssistEditor::updateButtons()
{
    const auto list = proc.reference.blocksCopy();
    const bool has = ! list.empty(), busy = proc.isAnalysing(), locked = list.isLocked();
    const int sel = blockEditor.selectedCount();

    loadButton.setEnabled (! busy);
    bigLoadButton.setVisible (! has && ! busy && tab == Tab::Perform);
    blockEditor.setVisible ((has || busy) && tab == Tab::Edit);
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
    focusBox.setEnabled (has);
    updateCycleLabel();
    repaint();
}

void AssistEditor::syncFocusBox()
{
    const int count = proc.score().phraseCount();
    if (count != lastPhraseCount)
    {
        lastPhraseCount = count;
        focusBox.clear (juce::dontSendNotification);
        focusBox.addItem ("All phrases", 1);
        for (int i = 0; i < std::min (count, 98); ++i)
            focusBox.addItem ("Phrase " + juce::String (i + 1), i + 2);
    }
    const int wanted = (int) std::lround (proc.apvts.getRawParameterValue ("focus")->load()) + 1;
    const int shown = wanted <= count + 1 ? wanted : 1;
    if (focusBox.getSelectedId() != shown)
        focusBox.setSelectedId (shown, juce::dontSendNotification);
    updateCycleLabel();
}

void AssistEditor::updateCycleLabel()
{
    const auto cfg = proc.score().settings();
    double a = 0, b = 0;
    const bool focused = cfg.focusPhrase >= 0 && proc.score().phraseRange (cfg.focusPhrase, a, b);
    const bool show = focused && ! proc.isAnalysing();
    copyCycleButton.setVisible (show);
    cycleLabel.setVisible (show);
    if (focused)
        cycleLabel.setText ("Set Cubase's cycle to " + clock (a) + " - " + clock (b), juce::dontSendNotification);
}

void AssistEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateButtons();
    auto err = proc.takeAnalysisError();
    if (err.isNotEmpty())
        showMessage ("Analysis failed", err);
    repaint();
}

void AssistEditor::timerCallback()
{
    progressValue = (double) proc.getAnalysisProgress();
    if (proc.isAnalysing() != progressBar.isVisible())
        updateButtons();
    if (proc.peekAnalysisError().isNotEmpty())
        changeListenerCallback (nullptr);

    syncFocusBox();
    switch (tab)
    {
        case Tab::Perform:  gameView.tick(); gameView.repaint(); break;
        case Tab::Edit:     blockEditor.tick(); break;
        default: break;
    }
    takesView.tick();
}

void AssistEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Performance Assist", 20, 12, 260, 30, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff7fb2ff));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (juce::String ("Version ") + VA_VERSION_LABEL, 280, 12, 400, 30, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff9aa0ab));
    g.setFont (juce::FontOptions (13.0f));
    const auto info = proc.reference.infoCopy();
    if (proc.isAnalysing())
        g.drawText ("Analysing " + proc.getAnalysisFileName() + " ...", statusArea.withTrimmedRight (200), juce::Justification::centredLeft);
    else if (info.kind != va::SourceKind::None)
    {
        const auto list = proc.reference.blocksCopy();
        const char* quality = info.kind == va::SourceKind::Midi ? "MIDI, exact"
                              : info.quality == va::Quality::Green ? "clean audio"
                              : info.quality == va::Quality::Amber ? "usable audio" : "rough audio, check the blocks";
        g.drawText ("Reference: " + info.sourceName + "   " + juce::String ((int) list.blocks().size()) + " notes   ("
                        + quality + ")   " + juce::String (proc.score().phraseCount()) + " phrases",
                    statusArea.withTrimmedRight (510), juce::Justification::centredLeft);
    }
}
