// Casti editoru: zalozky skupin, patchbay, detail ovladace, kontrola po fotce, obnova, prohlizec fotek
#include "PluginEditor.h"
#include "Assets.h"

using juce::String;
using juce::Colour;
using rm::ControlType;

namespace
{
const Colour kBg      (0xff0b0c0e);
const Colour kField   (0xff23262c);
const Colour kText    (0xffe8eaed);
const Colour kDim     (0xff8b929e);
const Colour kAccent  (0xffffb020);
const Colour kPhoto   (0xff35c8ff);
const Colour kOk      (0xff2f9e5b);

juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }

void style (juce::TextButton& b, const String& text, Colour c = Colour (0xff2a2e35))
{
    b.setButtonText (text);
    b.setColour (juce::TextButton::buttonColourId, c);
    b.setColour (juce::TextButton::textColourOffId, c == kOk || c == kAccent ? Colour (0xff111111) : kText);
}

float textWidth (const juce::Font& f, const String& t) { return juce::GlyphArrangement::getStringWidth (f, t); }

// jednoduchy dialog s jednim radkem textu (asynchronni)
void askLine (juce::Component* owner, const String& title, const String& initial, std::function<void (const String&)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, owner);
    w->addTextEditor ("t", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (String::fromUTF8 ("Zrušit"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        if (r == 1 && done) done (w->getTextEditorContents ("t"));
    }), true);
}
}

//==============================================================================
// PhotoViewer
//==============================================================================
PhotoViewer::PhotoViewer()
{
    style (alignButton, U ("Zarovnat ručně (4 rohy)"));
    style (closeButton, U ("Zavřít"));
    alignButton.onClick = [this] { if (onAlignRequest) onAlignRequest(); };
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (alignButton);
    addAndMakeVisible (closeButton);
    setWantsKeyboardFocus (true);
}

void PhotoViewer::close()
{
    aligning = false; clicks.clear();
    setVisible (false);
}

void PhotoViewer::startAlign (const String& deviceName)
{
    aligning = true; clicks.clear(); alignName = deviceName;
    repaint();
}

juce::Rectangle<float> PhotoViewer::imageRect() const
{
    auto b = getLocalBounds().reduced (16).withTrimmedTop (34).toFloat();
    if (! image.isValid()) return b;
    const float k = juce::jmin (1.0f, b.getWidth() / (float) image.getWidth(), b.getHeight() / (float) image.getHeight());
    return juce::Rectangle<float> ((float) image.getWidth() * k, (float) image.getHeight() * k).withCentre (b.getCentre());
}

void PhotoViewer::resized()
{
    auto top = getLocalBounds().reduced (16).removeFromTop (28);
    closeButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    alignButton.setBounds (top.removeFromRight (200));
}

void PhotoViewer::paint (juce::Graphics& g)
{
    g.fillAll (Colour (0xf20b0c0e));
    auto b = getLocalBounds().reduced (16);
    g.setColour (kText); g.setFont (font (14.0f, true));
    const String head = aligning ? U ("Klikni na roh panelu ") + alignName + ": "
                                       + (clicks.size() == 0 ? U ("VLEVO NAHOŘE") : clicks.size() == 1 ? U ("VPRAVO NAHOŘE")
                                          : clicks.size() == 2 ? U ("VPRAVO DOLE") : U ("VLEVO DOLE")) + U ("   (Esc = zrušit)")
                                 : caption;
    g.drawText (head, b.removeFromTop (28).withTrimmedRight (310), juce::Justification::centredLeft);
    if (! image.isValid()) return;
    const auto r = imageRect();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image, r, juce::RectanglePlacement::stretchToFit);
    const float k = r.getWidth() / (float) image.getWidth();
    auto toView = [&] (juce::Point<float> p) { return juce::Point<float> (r.getX() + p.x * k, r.getY() + p.y * k); };
    if (! aligning && quad.size() == 4)
    {
        // kde plugin pristroj nasel (kontrola zarovnani okem)
        juce::Path q;
        q.startNewSubPath (toView (quad[0]));
        for (int i = 1; i < 4; ++i) q.lineTo (toView (quad[(size_t) i]));
        q.closeSubPath();
        g.setColour (Colour (0xcc000000)); g.strokePath (q, juce::PathStrokeType (4.0f));
        g.setColour (kPhoto);              g.strokePath (q, juce::PathStrokeType (2.0f));
    }
    if (aligning)
    {
        for (size_t i = 0; i < clicks.size(); ++i)
        {
            const auto p = toView (clicks[i]);
            g.setColour (Colour (0xff000000)); g.fillEllipse (p.x - 8, p.y - 8, 16, 16);
            g.setColour (kAccent);             g.fillEllipse (p.x - 6, p.y - 6, 12, 12);
            if (i > 0) { g.drawLine (juce::Line<float> (toView (clicks[i - 1]), p), 2.0f); }
        }
    }
}

void PhotoViewer::mouseUp (const juce::MouseEvent& e)
{
    if (! aligning) { close(); return; }
    const auto r = imageRect();
    if (! r.contains (e.position) || ! image.isValid()) return;
    const float k = (float) image.getWidth() / r.getWidth();
    clicks.push_back ({ (e.position.x - r.getX()) * k, (e.position.y - r.getY()) * k });
    repaint();
    if (clicks.size() == 4)
    {
        std::array<juce::Point<float>, 4> c { clicks[0], clicks[1], clicks[2], clicks[3] };
        aligning = false; clicks.clear();
        if (onCorners) onCorners (c);
    }
}

bool PhotoViewer::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (aligning) { aligning = false; clicks.clear(); repaint(); } else close(); return true; }
    return false;
}

//==============================================================================
// GroupTabs
//==============================================================================
void GroupTabs::paint (juce::Graphics& g)
{
    g.fillAll (Colour (0xff111316));
    rects.clear();
    int x = 8;
    const auto f = font (13.5f, true);
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto& t = tabs[i];
        const bool pb = t.id == "@patchbay";
        const int w = (int) textWidth (f, t.name) + 26;
        juce::Rectangle<int> r (x, 4, w, getHeight() - 4);
        rects.push_back (r);
        const bool sel = t.id == selected, hov = hover == (int) i;
        g.setColour (sel ? Colour (0xff2c313a) : hov ? Colour (0xff1d2026) : Colour (0xff15171b));
        g.fillRoundedRectangle (r.toFloat().withTrimmedBottom (-4.0f), 5.0f);
        if (sel) { g.setColour (pb ? kPhoto : kAccent); g.fillRect (r.getX() + 6, r.getBottom() - 3, r.getWidth() - 12, 3); }
        g.setColour (sel ? kText : kDim); g.setFont (f);
        g.drawText (t.name, r, juce::Justification::centred);
        x += w + (pb ? 14 : 4);
    }
    addRect = { x, 4, 28, getHeight() - 4 };
    g.setColour (hover == -1 ? kAccent : kDim); g.setFont (font (18.0f, true));
    g.drawText ("+", addRect, juce::Justification::centred);

    if (status.isNotEmpty())
    {
        auto sr = getLocalBounds().withTrimmedLeft (addRect.getRight() + 16).reduced (8, 0);
        g.setColour (statusColour); g.setFont (font (13.0f, true));
        g.drawFittedText (status, sr, juce::Justification::centredRight, 1);
    }
}

juce::Rectangle<int> GroupTabs::rectFor (const String& id) const
{
    for (size_t i = 0; i < tabs.size() && i < rects.size(); ++i) if (tabs[i].id == id) return rects[i];
    return {};
}

void GroupTabs::mouseMove (const juce::MouseEvent& e)
{
    int h = -2;
    for (size_t i = 0; i < rects.size(); ++i) if (rects[i].contains (e.getPosition())) h = (int) i;
    if (addRect.contains (e.getPosition())) h = -1;
    if (h != hover) { hover = h; repaint(); }
}

void GroupTabs::mouseUp (const juce::MouseEvent& e)
{
    if (addRect.contains (e.getPosition())) { if (onAdd) onAdd(); return; }
    for (size_t i = 0; i < rects.size() && i < tabs.size(); ++i)
        if (rects[i].contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu()) { if (onMenu) onMenu (tabs[i].id); }
            else if (onSelect) onSelect (tabs[i].id);
            return;
        }
}

//==============================================================================
// RecoveryBanner
//==============================================================================
RecoveryBanner::RecoveryBanner()
{
    style (restore, U ("Obnovit"), kAccent);
    style (discard, U ("Zahodit"));
    restore.onClick = [this] { if (onRestore) onRestore(); };
    discard.onClick = [this] { if (onDiscard) onDiscard(); };
    addAndMakeVisible (restore); addAndMakeVisible (discard);
}

void RecoveryBanner::paint (juce::Graphics& g)
{
    g.setColour (Colour (0xff3a2a08)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    g.setColour (kAccent); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.2f);
    g.setColour (kText); g.setFont (font (13.5f, true));
    g.drawFittedText (text, getLocalBounds().reduced (12, 2).withTrimmedRight (200), juce::Justification::centredLeft, 2);
}

void RecoveryBanner::resized()
{
    auto b = getLocalBounds().reduced (6);
    discard.setBounds (b.removeFromRight (86)); b.removeFromRight (6);
    restore.setBounds (b.removeFromRight (96));
}

//==============================================================================
// PatchbayView
//==============================================================================
namespace
{
const Colour kCable[] = { Colour (0xffe8453c), Colour (0xff3d8bff), Colour (0xffffc23a), Colour (0xff3ccf6a),
                          Colour (0xffc65cff), Colour (0xffff8a2a), Colour (0xff2fd6d0), Colour (0xfff2f2f2) };
const int kCaption = 22, kPanelGap = 12, kAddH = 34, kRowH = 24;
}

PatchbayView::PatchbayView (RackMemoryProcessor& p) : proc (p) {}

void PatchbayView::refresh()
{
    std::vector<Panel> np;
    for (auto& pb : proc.studio.patchbays)
    {
        Panel pn; pn.id = pb.id; pn.prof = rm::findProfile (pb.model);
        if (pn.prof == nullptr) continue;
        for (auto& old : panels) if (old.id == pn.id) pn.img = old.img;
        if (! pn.img.isValid()) pn.img = rm::loadAsset (pn.prof->image);
        np.push_back (pn);
    }
    panels = np;
    if (pending.isNotEmpty() && panelOf (pending) == nullptr) pending.clear();
    resized();
    repaint();
}

int PatchbayView::naturalHeight (int width) const
{
    const float W = (float) width - 16.0f;
    float y = 8;
    for (auto& pb : proc.studio.patchbays)
        if (auto* prof = rm::findProfile (pb.model)) y += kCaption + W * prof->h / prof->w + kPanelGap;
    y += kAddH + 10;
    y += 30 + kRowH * (float) juce::jmax (2, (int) proc.working.cables.size()) + 10;
    return (int) y;
}

void PatchbayView::resized()
{
    const float W = (float) getWidth() - 16.0f;
    float y = 8;
    for (auto& pn : panels)
    {
        y += kCaption;
        const float h = W * pn.prof->h / pn.prof->w;
        pn.r = { 8.0f, y, W, h };
        y += h + kPanelGap;
    }
    addRect = { 8, (int) y, juce::jmin (240, getWidth() / 3), kAddH };
    y += kAddH + 10;
    listArea = juce::Rectangle<int> (8, (int) y, getWidth() - 16, juce::jmax (40, getHeight() - (int) y - 4));
}

const PatchbayView::Panel* PatchbayView::panelOf (const String& pointId) const
{
    const auto pb = pointId.upToLastOccurrenceOf (":", false, false);
    for (auto& p : panels) if (p.id == pb) return &p;
    return nullptr;
}

juce::Point<float> PatchbayView::pointPosition (const String& pointId) const
{
    auto* pn = panelOf (pointId);
    if (pn == nullptr) return {};
    const auto spec = pointId.fromLastOccurrenceOf (":", false, false);
    const int ch = spec.substring (1).getIntValue();
    const auto& pb = pn->prof->patchbay;
    const float k = pn->r.getWidth() / pn->prof->w;
    return { pn->r.getX() + (pb.x0 + (float) (ch - 1) * pb.dx) * k, pn->r.getY() + (spec.startsWithChar ('T') ? pb.topY : pb.bottomY) * k };
}

juce::Rectangle<float> PatchbayView::modeArea (const Panel& pn, int ch) const
{
    const auto& pb = pn.prof->patchbay;
    const float k = pn.r.getWidth() / pn.prof->w;
    const float x = pn.r.getX() + (pb.x0 + (float) (ch - 1) * pb.dx) * k, y = pn.r.getY() + (pb.topY + pb.bottomY) * 0.5f * k;
    return juce::Rectangle<float> (pb.dx * k * 0.8f, (pb.bottomY - pb.topY) * k * 0.34f).withCentre ({ x, y });
}

String PatchbayView::pointAt (juce::Point<float> p) const
{
    for (auto& pn : panels)
    {
        if (! pn.r.expanded (4).contains (p)) continue;
        const auto& pb = pn.prof->patchbay;
        const float k = pn.r.getWidth() / pn.prof->w;
        for (int ch = 1; ch <= pb.channels; ++ch)
            for (auto row : { 'T', 'B' })
            {
                const String id = pn.id + ":" + String::charToString (row) + String (ch);
                if (pointPosition (id).getDistanceFrom (p) <= juce::jmax (6.0f, pb.jackR * k * 1.25f)) return id;
            }
    }
    return {};
}

String PatchbayView::labelOf (const String& pointId) const
{
    const auto pbId = pointId.upToLastOccurrenceOf (":", false, false);
    for (auto& pb : proc.studio.patchbays)
        if (pb.id == pbId)
        {
            auto it = pb.labels.find (pointId.fromLastOccurrenceOf (":", false, false));
            return it != pb.labels.end() ? it->second : String();
        }
    return {};
}

String PatchbayView::pointName (const String& pointId) const
{
    auto* pn = panelOf (pointId);
    if (pn == nullptr) return pointId;
    const auto spec = pointId.fromLastOccurrenceOf (":", false, false);
    const int ch = spec.substring (1).getIntValue();
    const bool bottom = spec.startsWithChar ('B');
    String n = pn->prof->name.fromFirstOccurrenceOf (" ", false, false);
    if (pn->id.containsChar ('#')) n << " (" << pn->id.fromLastOccurrenceOf ("#", false, false) << ")";
    n << " " << (bottom && pn->prof->patchbay.bottomContinues ? String (ch + pn->prof->patchbay.channels) : String (ch) + (bottom ? U (" dole") : U (" nahoře")));
    const auto lab = labelOf (pointId);
    if (lab.isNotEmpty()) n << U (" · ") << lab;
    return n;
}

void PatchbayView::setLabel (const String& pointId, const String& text)
{
    auto st = proc.studio;
    if (auto* pb = st.findPatchbay (pointId.upToLastOccurrenceOf (":", false, false)))
    {
        const auto key = pointId.fromLastOccurrenceOf (":", false, false);
        if (text.trim().isEmpty()) pb->labels.erase (key); else pb->labels[key] = text.trim().substring (0, 40);
        proc.setStudio (st);
        if (onChanged) onChanged();
    }
}

void PatchbayView::setMode (const String& pbId, int channel, const String& mode)
{
    auto st = proc.studio;
    if (auto* pb = st.findPatchbay (pbId))
    {
        auto* prof = rm::findProfile (pb->model);
        if (prof != nullptr && ! prof->patchbay.modes.isEmpty() && mode == prof->patchbay.modes[0]) pb->modes.erase (channel);
        else pb->modes[channel] = mode;
        proc.setStudio (st);
        if (onChanged) onChanged();
    }
}

void PatchbayView::clickPoint (const String& id)
{
    if (id.isEmpty()) return;
    if (pending.isEmpty()) { pending = id; if (onStatus) onStatus (U ("Kabel z ") + pointName (id) + U (" → klikni na druhý konec (znovu = zrušit)")); repaint(); return; }
    if (pending == id) { pending.clear(); if (onStatus) onStatus ({}); repaint(); return; }
    auto cables = proc.working.cables;
    for (auto& c : cables)
        if ((c.from == pending && c.to == id) || (c.from == id && c.to == pending))
        { pending.clear(); if (onStatus) onStatus (U ("Tenhle kabel už je zapojený.")); repaint(); return; }
    cables.push_back ({ pending, id, {} });
    if (onStatus) onStatus (U ("Kabel: ") + pointName (pending) + U ("  →  ") + pointName (id));
    pending.clear();
    proc.setCables (cables);
    if (onChanged) onChanged();
}

void PatchbayView::editLabel (const String& pointId)
{
    juce::Component::SafePointer<PatchbayView> safe (this);
    askLine (this, U ("Popis bodu ") + pointName (pointId).upToFirstOccurrenceOf (U (" · "), false, false), labelOf (pointId),
             [safe, pointId] (const String& t) { if (safe != nullptr) safe->setLabel (pointId, t); });
}

void PatchbayView::pointMenu (const String& pointId)
{
    auto* pn = panelOf (pointId);
    if (pn == nullptr) return;
    const int ch = pointId.fromLastOccurrenceOf (":", false, false).substring (1).getIntValue();
    juce::PopupMenu m, assign, modes;
    m.addSectionHeader (pointName (pointId));
    m.addItem (1, U ("Popsat…"));
    // pristroje ze vsech skupin: nejdriv VSTUP, pak VYSTUP
    juce::StringArray texts;
    for (auto& id : proc.studio.allInstances())
    {
        auto name = proc.studio.groups.front().layout.displayName (id);
        if (auto* p = rm::profileForInstance (id)) name = p->name.fromFirstOccurrenceOf (" ", false, false);
        if (id.containsChar ('#')) name << " (" << id.fromLastOccurrenceOf ("#", false, false) << ")";
        for (auto suffix : { " IN", " OUT" }) { texts.add (name + suffix); assign.addItem (100 + texts.size() - 1, name + suffix); }
    }
    for (auto t : { "Interface IN", "Interface OUT", "Mix bus" }) { texts.add (t); assign.addItem (100 + texts.size() - 1, t); }
    m.addSubMenu (U ("Přiřadit"), assign);
    const auto& spec = pn->prof->patchbay;
    String current = spec.modes.isEmpty() ? String() : spec.modes[0];
    for (auto& pb : proc.studio.patchbays) if (pb.id == pn->id && pb.modes.count (ch)) current = pb.modes.at (ch);
    for (int i = 0; i < spec.modes.size(); ++i)
        modes.addItem (300 + i, spec.modes[i] + "  " + spec.modeNames[i], true, spec.modes[i] == current);
    if (spec.modes.size() > 0) m.addSubMenu (U ("Režim kanálu ") + String (ch), modes);
    int n = 0; for (auto& c : proc.working.cables) if (c.from == pointId || c.to == pointId) ++n;
    m.addItem (2, U ("Odpojit kabely (") + String (n) + ")", n > 0);
    m.addItem (3, U ("Smazat popis"), labelOf (pointId).isNotEmpty());

    juce::Component::SafePointer<PatchbayView> safe (this);
    const auto pbId = pn->id;
    const auto modeList = spec.modes;
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, pointId, texts, pbId, ch, modeList] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) safe->editLabel (pointId);
        else if (r == 3) safe->setLabel (pointId, {});
        else if (r == 2)
        {
            auto c = safe->proc.working.cables;
            c.erase (std::remove_if (c.begin(), c.end(), [&] (const rm::Cable& x) { return x.from == pointId || x.to == pointId; }), c.end());
            safe->proc.setCables (c);
            if (safe->onChanged) safe->onChanged();
        }
        else if (r >= 300) safe->setMode (pbId, ch, modeList[r - 300]);
        else if (r >= 100) safe->setLabel (pointId, texts[r - 100]);
    });
}

void PatchbayView::panelMenu (const Panel& pn)
{
    juce::PopupMenu m;
    m.addSectionHeader (pn.prof->name);
    if (pn.prof->patchbay.modeHow.isNotEmpty()) m.addItem (9, pn.prof->patchbay.modeHow, false);
    m.addItem (1, U ("Odebrat patchbay (popisky se smažou, kabely v projektu zůstanou)"));
    juce::Component::SafePointer<PatchbayView> safe (this);
    const auto id = pn.id;
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, id] (int r)
    {
        if (safe == nullptr || r != 1) return;
        auto st = safe->proc.studio;
        st.patchbays.erase (std::remove_if (st.patchbays.begin(), st.patchbays.end(), [&] (const rm::PatchbayConfig& p) { return p.id == id; }), st.patchbays.end());
        safe->proc.setStudio (st);
        safe->refresh();
        if (safe->onChanged) safe->onChanged();
    });
}

void PatchbayView::showAddMenu()
{
    juce::PopupMenu m;
    std::vector<String> ids;
    for (auto& p : rm::profiles()) if (p.isPatchbay()) { ids.push_back (p.id); m.addItem ((int) ids.size(), p.name); }
    juce::Component::SafePointer<PatchbayView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (addRect)), [safe, ids] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto st = safe->proc.studio;
        st.addPatchbay (ids[(size_t) r - 1]);
        safe->proc.setStudio (st);
        safe->refresh();
        if (safe->onChanged) safe->onChanged();
    });
}

void PatchbayView::mouseDown (const juce::MouseEvent& e)
{
    const auto id = pointAt (e.position);
    if (id.isNotEmpty())
    {
        if (e.mods.isPopupMenu()) pointMenu (id);
        else clickPoint (id);
        return;
    }
    for (auto& pn : panels)
    {
        if (! pn.r.contains (e.position)) continue;
        // Samson: prepinac rezimu kanalu vpredu -> klik prepne
        if (! e.mods.isPopupMenu() && pn.prof->patchbay.modeHow.startsWith (U ("Přepínač")))
            for (int ch = 1; ch <= pn.prof->patchbay.channels; ++ch)
                if (modeArea (pn, ch).contains (e.position))
                {
                    const auto& modes = pn.prof->patchbay.modes;
                    String cur = modes[0];
                    for (auto& pb : proc.studio.patchbays) if (pb.id == pn.id && pb.modes.count (ch)) cur = pb.modes.at (ch);
                    setMode (pn.id, ch, modes[(modes.indexOf (cur) + 1) % modes.size()]);
                    return;
                }
        if (e.mods.isPopupMenu()) panelMenu (pn);
        return;
    }
    if (addRect.contains (e.getPosition())) { showAddMenu(); return; }
    for (auto& d : deleteAreas)
        if (d.first.contains (e.position))
        {
            auto c = proc.working.cables;
            if (d.second >= 0 && d.second < (int) c.size()) c.erase (c.begin() + d.second);
            proc.setCables (c);
            if (onChanged) onChanged();
            return;
        }
    if (pending.isNotEmpty()) { pending.clear(); if (onStatus) onStatus ({}); repaint(); }
}

void PatchbayView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto id = pointAt (e.position);
    if (id.isNotEmpty()) { pending.clear(); editLabel (id); }
}

void PatchbayView::mouseMove (const juce::MouseEvent& e)
{
    const auto id = pointAt (e.position);
    if (id != hoverPoint) { hoverPoint = id; repaint(); }
    setMouseCursor (id.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PatchbayView::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    if (panels.empty())
    {
        g.setColour (kDim); g.setFont (font (14.0f));
        g.drawFittedText (U ("Žádný patchbay. Přidej ho tlačítkem dole (Samson S-patch Plus, Neutrik NYS-SPP-L1).\n"
                             "Pak klikni na jeden jack a na druhý = kabel. Dvojklik na jack = popis (co je zezadu zapojené)."),
                          getLocalBounds().reduced (16).withHeight (60), juce::Justification::topLeft, 3);
    }
    std::map<String, int> modesShown;
    for (auto& pn : panels)
    {
        const auto& spec = pn.prof->patchbay;
        auto cap = juce::Rectangle<float> (pn.r.getX(), pn.r.getY() - (float) kCaption, pn.r.getWidth(), (float) kCaption);
        g.setColour (kText); g.setFont (font (13.0f, true));
        String n = pn.prof->name;
        if (pn.id.containsChar ('#')) n << " (" << pn.id.fromLastOccurrenceOf ("#", false, false) << ")";
        g.drawText (n, cap, juce::Justification::centredLeft);
        g.setColour (kDim); g.setFont (font (12.0f));
        g.drawText (spec.topLabel + U ("   ·   ") + spec.bottomLabel, cap.withTrimmedLeft (textWidth (font (13.0f, true), n) + 14), juce::Justification::centredLeft);

        g.drawImage (pn.img, pn.r, juce::RectanglePlacement::stretchToFit);
        const float k = pn.r.getWidth() / pn.prof->w;
        const rm::PatchbayConfig* cfg = nullptr;
        for (auto& pb : proc.studio.patchbays) if (pb.id == pn.id) cfg = &pb;

        for (int ch = 1; ch <= spec.channels; ++ch)
        {
            // rezim kanalu (jen kdyz neni vychozi, u Samsonu vzdy na prepinaci)
            String mode = spec.modes.isEmpty() ? String() : spec.modes[0];
            if (cfg != nullptr && cfg->modes.count (ch)) mode = cfg->modes.at (ch);
            const bool frontSwitch = spec.modeHow.startsWith (U ("Přepínač"));
            if (mode.isNotEmpty() && (frontSwitch || mode != spec.modes[0]))
            {
                auto ma = modeArea (pn, ch);
                g.setColour (mode != spec.modes[0] ? kAccent : Colour (0xffeef1f6)); g.fillRoundedRectangle (ma, 2.0f);
                g.setColour (Colour (0xff10131a)); g.setFont (font (juce::jlimit (7.0f, 12.0f, ma.getHeight() * 0.85f), true));
                g.drawText (mode, ma, juce::Justification::centred);
            }
            for (auto row : { 'T', 'B' })
            {
                const String id = pn.id + ":" + String::charToString (row) + String (ch);
                const auto p = pointPosition (id);
                const float jr = spec.jackR * k;
                const auto lab = labelOf (id);
                if (lab.isNotEmpty())
                {
                    // popisek: nad horni radou / pod dolni radou
                    auto f = font (juce::jlimit (8.0f, 11.0f, spec.dx * k * 0.3f), true);
                    const float w = spec.dx * k - 2.0f, h = f.getHeight() + 1.0f;
                    auto tag = juce::Rectangle<float> (w, h).withCentre ({ p.x, row == 'T' ? p.y - jr - h * 0.5f - 1.0f : p.y + jr + h * 0.5f + 1.0f });
                    g.setColour (Colour (0xe0000000)); g.fillRoundedRectangle (tag, 2.0f);
                    g.setColour (kText); g.setFont (f);
                    g.drawFittedText (lab, tag.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                }
                if (id == pending || id == hoverPoint)
                {
                    g.setColour (id == pending ? kAccent : kPhoto);
                    g.drawEllipse (p.x - jr - 3, p.y - jr - 3, (jr + 3) * 2, (jr + 3) * 2, 2.4f);
                }
            }
        }
    }

    // kabely pres panely
    int ci = 0;
    for (auto& c : proc.working.cables)
    {
        const auto a = pointPosition (c.from), b = pointPosition (c.to);
        const Colour col = kCable[ci++ % 8];
        if (a.isOrigin() || b.isOrigin()) continue;
        const float sag = 18.0f + 0.18f * a.getDistanceFrom (b);
        juce::Path path;
        path.startNewSubPath (a);
        path.cubicTo (a.translated (0, sag), b.translated (0, sag), b);
        g.setColour (Colour (0xa0000000)); g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);                 g.strokePath (path, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (auto p : { a, b }) { g.setColour (Colour (0xff000000)); g.fillEllipse (p.x - 6, p.y - 6, 12, 12); g.setColour (col); g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9, 9); }
    }

    // + patchbay
    g.setColour (Colour (0xff15171b)); g.fillRoundedRectangle (addRect.toFloat(), 5.0f);
    g.setColour (Colour (0xff4a515d)); g.drawRoundedRectangle (addRect.toFloat(), 5.0f, 1.2f);
    g.setColour (kText); g.setFont (font (13.0f, true));
    g.drawText (U ("+ patchbay"), addRect, juce::Justification::centred);

    // seznam kabelu (zdroj -> cil)
    deleteAreas.clear();
    auto list = listArea.toFloat();
    g.setColour (kDim); g.setFont (font (12.5f, true));
    g.drawText (U ("KABELY V TOMTO PROJEKTU  (zdroj → cíl; ukládají se s projektem a s verzemi)"), list.removeFromTop (24.0f), juce::Justification::centredLeft);
    if (proc.working.cables.empty())
    {
        g.setFont (font (12.5f)); g.drawText (U ("zatím žádný kabel"), list.removeFromTop ((float) kRowH), juce::Justification::centredLeft);
    }
    for (size_t i = 0; i < proc.working.cables.size(); ++i)
    {
        auto row = list.removeFromTop ((float) kRowH);
        if (row.getHeight() < 10.0f) break;
        auto& c = proc.working.cables[i];
        g.setColour (kCable[i % 8]); g.fillEllipse (row.getX() + 2, row.getCentreY() - 5, 10, 10);
        auto del = juce::Rectangle<float> (row.getRight() - 22.0f, row.getY() + 3.0f, 18.0f, 18.0f);
        deleteAreas.push_back ({ del, (int) i });
        g.setColour (kDim); g.drawText (U ("×"), del, juce::Justification::centred);
        g.setColour (kText); g.setFont (font (13.0f));
        g.drawFittedText (pointName (c.from) + U ("   →   ") + pointName (c.to) + (c.note.isNotEmpty() ? "   (" + c.note + ")" : String()),
                          row.withTrimmedLeft (20).withTrimmedRight (30).toNearestInt(), juce::Justification::centredLeft, 1);
    }
    if (hoverPoint.isNotEmpty())
    {
        const auto p = pointPosition (hoverPoint);
        const auto t = pointName (hoverPoint);
        auto f = font (13.0f, true);
        auto r = juce::Rectangle<float> (textWidth (f, t) + 16, 22).withCentre ({ p.x, p.y - 34 }).constrainedWithin (getLocalBounds().toFloat());
        g.setColour (Colour (0xf0101114)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (kAccent); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (kText); g.setFont (f); g.drawText (t, r, juce::Justification::centred);
    }
}

//==============================================================================
// ControlDetail
//==============================================================================
ControlDetail::ControlDetail()
{
    style (confirmButton, U ("Potvrdit"), kOk);
    style (unknownButton, U ("? nezadáno"));
    style (typeButton, U ("Napsat hodnotu"));
    style (minusButton, U ("−"));
    style (plusButton, "+");
    style (closeButton, U ("Zavřít"));
    for (auto* b : { &confirmButton, &unknownButton, &typeButton, &minusButton, &plusButton, &closeButton }) addAndMakeVisible (*b);
    addAndMakeVisible (zoom);
    addChildComponent (photoZoom);
    photoZoom.setInterceptsMouseClicks (false, false);
    zoom.onEdit = [this] { refresh(); if (onEdit) onEdit(); };
    closeButton.onClick = [this] { setVisible (false); };
    minusButton.onClick = [this] { step (-1); };
    plusButton.onClick  = [this] { step (+1); };
    typeButton.onClick  = [this] { zoom.startEntry (index); };
    unknownButton.onClick = [this]
    {
        if (proc == nullptr) return;
        auto* prof = rm::profileForInstance (instance);
        proc->working.values.erase (rm::valueKey (instance, prof->controls[(size_t) index].id));
        refresh(); zoom.repaint(); if (onEdit) onEdit();
    };
    confirmButton.onClick = [this]
    {
        if (proc == nullptr) return;
        auto* prof = rm::profileForInstance (instance);
        auto key = rm::valueKey (instance, prof->controls[(size_t) index].id);
        auto it = proc->working.values.find (key);
        if (it != proc->working.values.end()) it->second.confirmed = true;
        refresh(); zoom.repaint(); if (onEdit) onEdit();
    };
    setWantsKeyboardFocus (true);
}

void ControlDetail::show (RackMemoryProcessor& p, const String& inst, int idx, const juce::Image& rectified)
{
    proc = &p; instance = inst; index = idx;
    auto* prof = rm::profileForInstance (inst);
    if (prof == nullptr || idx < 0 || idx >= (int) prof->controls.size()) return;
    auto& c = prof->controls[(size_t) idx];
    zoom.setData (prof, &p.working, inst);
    juce::Rectangle<float> area (c.x - c.r * 2.4f, c.y - c.r * 2.1f, c.r * 4.8f, c.r * 4.2f);
    if (c.draw.kind == "vbuttons")
        for (auto& pt : c.draw.points) area = area.getUnion (juce::Rectangle<float> (c.draw.w * 1.6f, c.draw.h * 1.6f).withCentre ({ pt.x, pt.y }));
    zoom.setFocus (area);
    photoZoom.setVisible (rectified.isValid());
    if (rectified.isValid())
    {
        photoZoom.setData (prof, &p.working, inst);
        photoZoom.setPhoto (rectified, true);
        photoZoom.setFocus (area);
    }
    refresh();
    setVisible (true);
    toFront (true);
    resized();
}

String ControlDetail::describe() const
{
    if (proc == nullptr) return {};
    auto* prof = rm::profileForInstance (instance);
    if (prof == nullptr) return {};
    auto& c = prof->controls[(size_t) index];
    const auto v = proc->working.get (instance, c.id);
    String s;
    s << (c.group.isNotEmpty() && c.group != "Switches" && c.group != "Global" && ! c.name.containsIgnoreCase (c.group) ? c.group + " " : String()) << c.name
      << U ("   (") << proc->rack.displayName (instance) << ")\n";
    s << U ("Hodnota: ") << (v.known ? rm::describeValue (c, v) : U ("NEZADÁNO (není to nula ani výchozí poloha)")) << "\n";
    if (v.known)
    {
        if (c.type == ControlType::knob) s << U ("Poloha: ") << rm::clockString (v.pos, c.angleMax - c.angleMin) << U (" (") << juce::roundToInt (v.pos * 100) << " %)";
        else if (c.numPositions() > 0)   s << U ("Poloha: ") << ((int) v.pos + 1) << " / " << c.numPositions();
        if (c.type == ControlType::knob && c.scale.empty()) s << U ("  – číselná hodnota neznámá (stupnice není ověřená), ukládá se poloha");
        s << "\n";
    }
    if (! c.scale.empty())
    {
        juce::StringArray labs;
        for (auto& m : c.scale) labs.add (m.label);
        s << U ("Stupnice (potisk): ") << labs.joinIntoString ("  ") << "\n";
    }
    else if (! c.options.isEmpty()) s << U ("Polohy: ") << c.options.joinIntoString ("  /  ") << "\n";
    else if (c.minLabel.isNotEmpty()) s << U ("Rozsah: ") << c.minLabel << U (" … ") << c.maxLabel << "\n";
    String state;
    if (! v.known)                         state = U ("nezadáno");
    else if (v.source == "recognized")     state = v.confirmed ? U ("z fotky – potvrzeno") : U ("z fotky – KE KONTROLE");
    else if (v.source == "preset")         state = U ("z presetu");
    else                                   state = U ("zadáno ručně");
    if (v.known && v.confidence >= 0)      state << U ("  (jistota ") << juce::roundToInt (v.confidence * 100) << " %)";
    s << U ("Stav: ") << state << "\n";
    if (! c.verified) s << U ("Pozor: rozsah / počet poloh není ověřený.\n");
    if (c.note.isNotEmpty()) s << c.note << "\n";
    return s;
}

void ControlDetail::refresh()
{
    if (proc == nullptr) return;
    auto* prof = rm::profileForInstance (instance);
    if (prof == nullptr) return;
    auto& c = prof->controls[(size_t) index];
    const auto v = proc->working.get (instance, c.id);
    confirmButton.setVisible (v.known && ! v.confirmed);
    const bool steps = c.type != ControlType::button;
    minusButton.setVisible (steps); plusButton.setVisible (steps);
    typeButton.setVisible (c.type != ControlType::button);
    photoZoom.repaint();
    repaint();
}

void ControlDetail::step (int dir)
{
    if (proc == nullptr) return;
    auto* prof = rm::profileForInstance (instance);
    auto& c = prof->controls[(size_t) index];
    auto key = rm::valueKey (instance, c.id);
    auto v = proc->working.get (key);
    rm::ControlValue n; n.known = true; n.source = "manual";
    if (c.type == ControlType::knob) n.pos = juce::jlimit (0.0, 1.0, (v.known ? v.pos : 0.5) + dir * 0.02);
    else n.pos = (double) juce::jlimit (0, juce::jmax (0, c.numPositions() - 1), (v.known ? (int) v.pos : (c.numPositions() - 1) / 2) + dir);
    proc->working.values[key] = n;
    zoom.repaint(); refresh();
    if (onEdit) onEdit();
}

void ControlDetail::resized()
{
    const int w = juce::jmin (getWidth() - 24, photoZoom.isVisible() ? 820 : 640), h = juce::jmin (getHeight() - 24, 360);
    card = juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
    auto b = card.reduced (14);
    auto buttons = b.removeFromBottom (32);
    b.removeFromBottom (10);
    const int side = juce::jmin (b.getHeight(), 260);
    zoom.setBounds (b.removeFromLeft (side).withSizeKeepingCentre (side, side));
    b.removeFromLeft (10);
    if (photoZoom.isVisible()) { photoZoom.setBounds (b.removeFromLeft (side).withSizeKeepingCentre (side, side)); b.removeFromLeft (10); }
    textArea = b;
    closeButton.setBounds (buttons.removeFromRight (80)); buttons.removeFromRight (6);
    minusButton.setBounds (buttons.removeFromLeft (36)); buttons.removeFromLeft (4);
    plusButton.setBounds (buttons.removeFromLeft (36)); buttons.removeFromLeft (10);
    typeButton.setBounds (buttons.removeFromLeft (130)); buttons.removeFromLeft (6);
    unknownButton.setBounds (buttons.removeFromLeft (100)); buttons.removeFromLeft (6);
    confirmButton.setBounds (buttons.removeFromLeft (90));
}

void ControlDetail::paint (juce::Graphics& g)
{
    g.fillAll (Colour (0xb0000000));
    g.setColour (Colour (0xff17191d)); g.fillRoundedRectangle (card.toFloat(), 8.0f);
    g.setColour (kAccent.withAlpha (0.7f)); g.drawRoundedRectangle (card.toFloat(), 8.0f, 1.2f);
    g.setColour (Colour (0xff000000)); g.fillRect (zoom.getBounds());
    if (photoZoom.isVisible())
    {
        g.setColour (kDim); g.setFont (font (11.0f));
        g.drawText (U ("tvoje fotka"), photoZoom.getBounds().translated (0, -14).withHeight (14), juce::Justification::centredLeft);
    }
    const auto text = describe();
    auto lines = juce::StringArray::fromLines (text);
    juce::AttributedString as;
    as.setWordWrap (juce::AttributedString::byWord);
    for (int i = 0; i < lines.size(); ++i)
    {
        const bool head = i == 0;
        const Colour c = head ? kText : lines[i].startsWith (U ("Stav: z fotky – KE")) ? kPhoto
                       : lines[i].startsWith (U ("Hodnota: NEZ")) ? kAccent : kText.withAlpha (0.85f);
        as.append (lines[i] + (i + 1 < lines.size() ? "\n" : ""), font (head ? 16.0f : 13.5f, head), c);
        if (head) as.append ("\n", font (6.0f), c);
    }
    juce::TextLayout tl;
    tl.createLayout (as, (float) textArea.getWidth());
    tl.draw (g, textArea.toFloat());
}

void ControlDetail::mouseUp (const juce::MouseEvent& e)
{
    if (! card.contains (e.getPosition())) setVisible (false);
}

bool ControlDetail::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { setVisible (false); return true; }
    if (k == juce::KeyPress::leftKey || k == juce::KeyPress::downKey) { step (-1); return true; }
    if (k == juce::KeyPress::rightKey || k == juce::KeyPress::upKey) { step (+1); return true; }
    return false;
}

//==============================================================================
// ReviewPanel
//==============================================================================
ReviewPanel::ReviewPanel()
{
    style (confirmAll, U ("Potvrdit vše"), kOk);
    style (closeButton, U ("Zavřít"));
    confirmAll.onClick = [this] { if (onConfirmAll) onConfirmAll(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (confirmAll); addAndMakeVisible (closeButton);
}

void ReviewPanel::setItems (const std::vector<Item>& it)
{
    items = it;
    bool any = false;
    for (auto& i : items) any = any || i.unconfirmed > 0;
    confirmAll.setVisible (any);
    repaint();
}

int ReviewPanel::preferredHeight() const   { return 40 + 44 * (int) juce::jmax ((size_t) 1, items.size()); }

void ReviewPanel::resized()
{
    auto b = getLocalBounds().reduced (10).removeFromTop (28);
    closeButton.setBounds (b.removeFromRight (80)); b.removeFromRight (6);
    confirmAll.setBounds (b.removeFromRight (120));
}

void ReviewPanel::paint (juce::Graphics& g)
{
    g.setColour (Colour (0xf2121418)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (kPhoto.withAlpha (0.7f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.2f);
    auto b = getLocalBounds().reduced (12, 8).toFloat();
    g.setColour (kText); g.setFont (font (14.0f, true));
    g.drawText (U ("Kontrola po fotce"), b.removeFromTop (24.0f).withTrimmedRight (220.0f), juce::Justification::centredLeft);
    links.clear();
    for (auto& it : items)
    {
        auto row = b.removeFromTop (44.0f);
        auto top = row.removeFromTop (20.0f);
        g.setColour (it.failed ? kAccent : kText); g.setFont (font (13.0f, true));
        String head = it.title + ":  " + (it.failed ? it.message : U ("přečteno ") + String (it.read) + "/" + String (it.readable));
        if (! it.failed && it.unconfirmed > 0) head << U ("  ·  modré = z fotky, zkontroluj a potvrď");
        g.drawText (head, top, juce::Justification::centredLeft);
        if (it.uncertain.isEmpty()) continue;
        g.setColour (kDim); g.setFont (font (12.5f));
        const String lead = U ("nejisté, ponecháno beze změny: ");
        g.drawText (lead, row.removeFromLeft (textWidth (font (12.5f), lead) + 4), juce::Justification::centredLeft);
        auto* prof = rm::profileForInstance (it.instance);
        for (auto& cid : it.uncertain)
        {
            auto* c = prof != nullptr ? prof->find (cid) : nullptr;
            const String name = c != nullptr ? c->name : cid;
            const float w = textWidth (font (12.5f, true), name) + 12;
            if (w > row.getWidth()) break;
            auto r = row.removeFromLeft (w);
            g.setColour (kPhoto); g.setFont (font (12.5f, true));
            g.drawText (name, r, juce::Justification::centredLeft);
            g.drawLine (r.getX(), r.getCentreY() + 8, r.getX() + w - 12, r.getCentreY() + 8, 1.0f);
            links.push_back ({ r, it.instance, cid });
        }
    }
}

void ReviewPanel::mouseUp (const juce::MouseEvent& e)
{
    for (auto& l : links) if (l.r.contains (e.position)) { if (onOpen) onOpen (l.instance, l.control); return; }
}
