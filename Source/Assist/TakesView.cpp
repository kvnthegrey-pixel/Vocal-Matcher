#include "TakesView.h"
#include "GameView.h"

namespace pa
{
namespace
{
juce::Colour heatColour (const va::BlockStat& b)
{
    if (! b.counted)
        return juce::Colour (0xff2a2d35);
    if (b.octaveFrac >= 0.33f)
        return juce::Colour (0xff9b59d6);
    if (! b.hasBias)
        return juce::Colour (0xff5a2a2a);                          // nothing pitched: not sung

    const float a = std::abs (b.biasCents);
    const juce::Colour centred (0xff3ecf6b);
    if (a < 8.0f)
        return centred;
    const float t = juce::jlimit (0.0f, 1.0f, (a - 8.0f) / 42.0f);
    return centred.interpolatedWith (b.biasCents < 0.0f ? juce::Colour (0xff3f8cff) : juce::Colour (0xffff6b3d), t);
}

juce::String formatScore (int score)
{
    auto s = juce::String (score);
    for (int i = s.length() - 3; i > 0; i -= 3)
        s = s.substring (0, i) + "," + s.substring (i);
    return s;
}
}

TakesView::TakesView (AssistProcessor& p) : proc (p)
{
    addAndMakeVisible (copyButton);
    addAndMakeVisible (clearButton);
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (juce::String (proc.takeStore().compText (proc.phraseSeconds())));
    };
    clearButton.onClick = [this]
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Clear all takes?")
                                          .withMessage ("This removes every saved take and the comp plan.")
                                          .withButton ("Clear").withButton ("Cancel"),
                                      [this] (int result)
                                      {
                                          if (result == 1)
                                          {
                                              proc.clearTakes();
                                              selected = 0;
                                              repaint();
                                          }
                                      });
    };
    blocks = proc.reference.blocksCopy().blocks();
}

void TakesView::resized()
{
    auto r = getLocalBounds().reduced (4);
    auto bar = r.removeFromBottom (32);
    clearButton.setBounds (bar.removeFromRight (130));
    bar.removeFromRight (8);
    copyButton.setBounds (bar.removeFromRight (160));
    r.removeFromBottom (6);

    listArea = r.removeFromLeft (330);
    r.removeFromLeft (12);
    heatArea = r.removeFromTop (std::max (150, r.getHeight() / 2 - 20));
    r.removeFromTop (10);
    textArea = r.removeFromTop (62);
    r.removeFromTop (6);
    phraseArea = r;
}

void TakesView::tick()
{
    if (proc.takesRevision() != seenRevision)
    {
        seenRevision = proc.takesRevision();
        blocks = proc.reference.blocksCopy().blocks();
        const auto& takes = proc.takeStore().takes();
        if (selected == 0 || proc.takeStore().find (selected) == nullptr)
            selected = takes.empty() ? 0 : takes.back().number;
        else if (! takes.empty())
            selected = takes.back().number;          // a new take: show it
        repaint();
    }
}

void TakesView::select (int takeNumber)
{
    selected = takeNumber;
    repaint();
}

void TakesView::mouseDown (const juce::MouseEvent& e)
{
    for (const auto& row : rowHits)
        if (row.first.contains (e.getPosition()))
        {
            select (row.second);
            return;
        }
}

juce::Rectangle<int> TakesView::cellRect (juce::Rectangle<int> area, int index) const
{
    const int perRow = std::max (1, (area.getWidth() - 4) / (cellW + cellGap));
    return { area.getX() + 2 + (index % perRow) * (cellW + cellGap), area.getY() + 2 + (index / perRow) * (cellH + cellGap), cellW, cellH };
}

void TakesView::mouseMove (const juce::MouseEvent& e)
{
    int found = -1;
    auto inner = heatArea.reduced (10, 10);
    inner.removeFromTop (22);
    for (size_t i = 0; i < blocks.size(); ++i)
        if (cellRect (inner, (int) i).contains (e.getPosition()))
        {
            found = (int) i;
            break;
        }
    if (found != hoverBlockIndex)
    {
        hoverBlockIndex = found;
        repaint (heatArea);
    }
}

void TakesView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));
    drawList (g, listArea);
    drawHeatmap (g, heatArea);

    // plain-language read of the selected take
    g.setColour (colours::panel());
    g.fillRoundedRectangle (textArea.toFloat(), 8.0f);
    const auto* take = proc.takeStore().find (selected);
    g.setColour (colours::text());
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (take ? juce::String (va::tendencyText (*take)) : juce::String ("Sing a pass in Cubase. When it ends or loops, it appears here as a take."),
                      textArea.reduced (14, 8), juce::Justification::centredLeft, 3);

    drawPhrases (g, phraseArea);
}

void TakesView::drawList (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (colours::panel());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto box = area.reduced (10, 8);
    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto header = box.removeFromTop (18);
    g.drawText ("TAKE", header.removeFromLeft (60), juce::Justification::centredLeft);
    g.drawText ("SCORE", header.removeFromLeft (100), juce::Justification::centredLeft);
    g.drawText ("ACCURACY", header, juce::Justification::centredLeft);

    rowHits.clear();
    const auto& takes = proc.takeStore().takes();
    const auto* best = proc.takeStore().best();
    for (int i = (int) takes.size() - 1; i >= 0; --i)         // newest first
    {
        if (box.getHeight() < rowHeight) break;
        const auto& t = takes[(size_t) i];
        auto row = box.removeFromTop (rowHeight);
        rowHits.push_back ({ row, t.number });
        if (t.number == selected)
        {
            g.setColour (juce::Colour (0xff2c3340));
            g.fillRoundedRectangle (row.toFloat().reduced (0, 1), 5.0f);
        }
        auto cell = row.reduced (6, 0);
        g.setColour (colours::text());
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("#" + juce::String (t.number), cell.removeFromLeft (54), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (formatScore (t.score), cell.removeFromLeft (100), juce::Justification::centredLeft);
        g.drawText (juce::String (t.accuracy) + "%", cell.removeFromLeft (70), juce::Justification::centredLeft);
        if (&t == best)
        {
            g.setColour (colours::perfect());
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("BEST", cell, juce::Justification::centredRight);
        }
    }
    if (takes.empty())
    {
        g.setColour (colours::dim());
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("No takes yet.", box.removeFromTop (24), juce::Justification::centredLeft, 1);
    }
}

void TakesView::drawHeatmap (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (colours::panel());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto box = area.reduced (10, 10);

    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto head = box.removeFromTop (22);
    g.drawText ("WHERE EACH NOTE LANDED (one square per note)", head.removeFromLeft (head.getWidth() / 2), juce::Justification::centredLeft);

    // legend
    {
        auto lg = head.removeFromRight (360);
        g.setFont (juce::FontOptions (10.0f));
        const juce::Colour swatches[] = { juce::Colour (0xff3f8cff), juce::Colour (0xff3ecf6b), juce::Colour (0xffff6b3d), juce::Colour (0xff9b59d6), juce::Colour (0xff2a2d35) };
        const char* names[] = { "flat", "centred", "sharp", "octave", "not sung" };
        for (int i = 0; i < 5; ++i)
        {
            auto item = lg.removeFromLeft (72);
            g.setColour (swatches[i]);
            g.fillRoundedRectangle (item.removeFromLeft (12).toFloat().withSizeKeepingCentre (12.0f, 12.0f), 2.0f);
            g.setColour (colours::dim());
            g.drawText (names[i], item.withTrimmedLeft (4), juce::Justification::centredLeft);
        }
    }

    const auto* take = proc.takeStore().find (selected);
    std::map<int, const va::BlockStat*> byId;
    if (take)
        for (const auto& b : take->blocks)
            byId[b.id] = &b;

    const auto vs = proc.viewSettings();
    int lastPhrase = -1;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto cell = cellRect (box, (int) i).toFloat();
        if (cell.getBottom() > (float) box.getBottom() + cellH) break;
        va::BlockStat none;
        none.id = blocks[i].id;
        const auto it = byId.find (blocks[i].id);
        const auto& stat = it != byId.end() ? *it->second : none;

        const int phrase = proc.score().phraseOfBlock (blocks[i].id);
        if (phrase != lastPhrase)
        {
            lastPhrase = phrase;
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.fillRect (cell.getX() - 2.0f, cell.getY(), 1.0f, cell.getHeight());       // a thin rule where a phrase starts
        }
        g.setColour (heatColour (stat));
        g.fillRoundedRectangle (cell, 3.0f);
        if (stat.counted && stat.grade == va::Grade::Perfect)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (cell, 3.0f, 1.2f);
        }
        if (stat.counted && stat.octaveFrac >= 0.33f)
        {
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("O", cell.toNearestInt(), juce::Justification::centred);
        }
    }

    // what is under the mouse
    auto info = area.reduced (10, 6).removeFromBottom (18);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (colours::text());
    if (hoverBlockIndex >= 0 && hoverBlockIndex < (int) blocks.size())
    {
        const auto& b = blocks[(size_t) hoverBlockIndex];
        const auto it = byId.find (b.id);
        juce::String text = GameView::noteName ((int) std::lround (b.midi() + 12.0 * vs.octaveShift))
                            + " at " + juce::String (b.startSec() + vs.refOffsetSec, 1) + " s: ";
        if (it == byId.end())
            text += "not sung in this take";
        else
        {
            const auto& s = *it->second;
            text += juce::String (va::gradeName (s.grade)).toLowerCase();
            if (s.octaveFrac >= 0.33f)
                text += ", sung an octave " + juce::String (s.octaveDir > 0 ? "up" : "down");
            else if (s.hasBias)
                text += ", " + juce::String (va::describeBias ((double) s.biasCents));
        }
        g.drawText (text, info, juce::Justification::centredLeft);
    }
    else if (take == nullptr)
        g.drawText ("Select a take on the left.", info, juce::Justification::centredLeft);
}

void TakesView::drawPhrases (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (colours::panel());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    auto box = area.reduced (14, 10);

    const auto secs = proc.phraseSeconds();
    const auto comp = proc.takeStore().comp (secs);
    const auto* take = proc.takeStore().find (selected);

    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto head = box.removeFromTop (20);
    g.drawText ("COMP PLAN: the best take for each phrase", head.removeFromLeft (head.getWidth() - 260), juce::Justification::centredLeft);
    if (comp.phrasesCovered > 0)
    {
        g.setColour (colours::perfect());
        g.drawText ("Projected accuracy " + juce::String ((int) std::lround (comp.projected)) + "%", head, juce::Justification::centredRight);
    }

    // header row of the table
    g.setColour (colours::dim());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    auto cols = box.removeFromTop (14);
    const int labelW = 78, bestW = 110;
    g.drawText ("PHRASE", cols.removeFromLeft (labelW), juce::Justification::centredLeft);
    auto bestCol = cols.removeFromRight (bestW);
    g.drawText ("BEST TAKE", bestCol, juce::Justification::centredRight);
    g.drawText (take ? "TAKE #" + juce::String (take->number) : juce::String ("THIS TAKE"), cols, juce::Justification::centredLeft);

    const int count = (int) comp.takeNumber.size();
    if (count == 0)
    {
        g.setColour (colours::dim());
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Phrase scores appear once you have sung a complete phrase.", box.removeFromTop (30), juce::Justification::centredLeft);
        return;
    }

    const int rowH = std::clamp (box.getHeight() / count, 14, 24);
    for (int p = 0; p < count; ++p)
    {
        if (box.getHeight() < rowH) break;
        auto row = box.removeFromTop (rowH);
        g.setColour (colours::text());
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Phrase " + juce::String (p + 1), row.removeFromLeft (labelW), juce::Justification::centredLeft);

        auto best = row.removeFromRight (bestW);
        if (comp.takeNumber[(size_t) p] != 0)
        {
            g.setColour (colours::perfect());
            g.drawText ("take " + juce::String (comp.takeNumber[(size_t) p]) + "  " + juce::String ((int) std::lround (comp.score[(size_t) p])) + "%",
                        best, juce::Justification::centredRight);
        }
        else
        {
            g.setColour (colours::dim());
            g.drawText ("not sung", best, juce::Justification::centredRight);
        }

        // the selected take's score as a bar
        auto bar = row.reduced (0, 3).withTrimmedRight (10).toFloat();
        g.setColour (juce::Colour (0xff15171c));
        g.fillRoundedRectangle (bar, 3.0f);
        const float s = take != nullptr && (size_t) p < take->phraseScore.size() ? take->phraseScore[(size_t) p] : -1.0f;
        if (s >= 0.0f)
        {
            const auto c = s >= 85.0f ? colours::perfect() : (s >= 60.0f ? colours::good() : colours::miss());
            g.setColour (c);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * s / 100.0f), 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String ((int) std::lround (s)) + "%", bar.toNearestInt().reduced (6, 0), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (colours::dim());
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("not sung in this take", bar.toNearestInt().reduced (6, 0), juce::Justification::centredLeft);
        }
    }
}
} // namespace pa
