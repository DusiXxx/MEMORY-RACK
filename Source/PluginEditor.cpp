#include "PluginEditor.h"
#include "Recognizer.h"
#include "Assets.h"
#include <thread>
#include "Updater.h"

using juce::String;
using juce::Colour;
using rm::ControlType;

namespace
{
const Colour kBg      (0xff0b0c0e);
const Colour kBar     (0xff17191d);
const Colour kField   (0xff23262c);
const Colour kText    (0xffe8eaed);
const Colour kDim     (0xff8b929e);
const Colour kAccent  (0xffffb020);
const Colour kUnknown (0xffffb020);
const Colour kFromPhoto (0xff35c8ff);
const Colour kOk      (0xff2f9e5b);

const int kBarHeight = 44;
const int kGap = 5;
const int kStripHeight = 86;
const int kLevelHeight = 32;
const char* const kPhotoTypes = "jpg;jpeg;png;gif;heic;heif;tif;tiff";

juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

using rm::loadAsset;

bool anyPhoto (const juce::StringArray& files)
{
    for (auto& f : files) if (juce::File (f).hasFileExtension (kPhotoTypes)) return true;
    return false;
}

bool is500 (const rm::DeviceProfile* p)       { return p != nullptr && p->formFactor.startsWith ("500"); }
int slotsOf (const rm::DeviceProfile* p)      { return is500 (p) ? 2 : 0; }

void drawGlow (juce::Graphics& g, juce::Point<float> p, float r, Colour c)
{
    juce::ColourGradient halo (c.withAlpha (0.75f), p.x, p.y, c.withAlpha (0.0f), p.x + r * 3.2f, p.y, true);
    halo.addColour (0.35, c.withAlpha (0.35f));
    g.setGradientFill (halo);
    g.fillEllipse (p.x - r * 3.2f, p.y - r * 3.2f, r * 6.4f, r * 6.4f);
    g.setColour (c);
    g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.fillEllipse (p.x - r * 0.45f, p.y - r * 0.5f, r * 0.9f, r * 0.9f);
}

void styleEditor (juce::TextEditor& e, const String& placeholder)
{
    e.setColour (juce::TextEditor::backgroundColourId, kField);
    e.setColour (juce::TextEditor::textColourId, kText);
    e.setColour (juce::TextEditor::outlineColourId, Colour (0x00000000));
    e.setColour (juce::TextEditor::focusedOutlineColourId, kAccent.withAlpha (0.7f));
    e.setColour (juce::CaretComponent::caretColourId, kText);
    e.setFont (font (15.0f));
    e.setIndents (8, 6);
    e.setTextToShowWhenEmpty (placeholder, kDim);
}

void styleButton (juce::TextButton& b, const String& text)
{
    b.setButtonText (text);
    b.setColour (juce::TextButton::buttonColourId, Colour (0xff2a2e35));
    b.setColour (juce::TextButton::buttonOnColourId, kAccent);
    b.setColour (juce::TextButton::textColourOffId, kText);
    b.setColour (juce::TextButton::textColourOnId, Colour (0xff111111));
}

using rm::versionValue;

bool runTool (const juce::StringArray& args, int timeoutMs = 120000)
{
    juce::ChildProcess p;
    return p.start (args, 0) && p.waitForProcessToFinish (timeoutMs) && p.getExitCode() == 0;
}
}

//==============================================================================
// PanelView
//==============================================================================
PanelView::~PanelView()
{
    entryClosing = true;
    entry.reset();
}

void PanelView::setData (const rm::DeviceProfile* p, rm::Snapshot* s, const String& inst)
{
    instanceId = inst.isNotEmpty() ? inst : (p != nullptr ? p->id : String());
    if (p != profile)
    {
        background = p != nullptr ? loadAsset (p->image) : juce::Image();
        scaledBackground = {};
        sprites.clear();
        if (p != nullptr)
            for (auto& ctl : p->controls)
                if (ctl.draw.sprite.isNotEmpty() && sprites.find (ctl.draw.sprite) == sprites.end())
                    sprites[ctl.draw.sprite] = loadAsset (ctl.draw.sprite);
        closeEntry();
    }
    profile = p; snap = s;
    hover = dragging = -1;
    repaint();
}

void PanelView::setShowValues (bool b)   { showValues = b; repaint(); }
void PanelView::setPhoto (const juce::Image& rectified, bool show)   { photo = rectified; showPhoto = show; repaint(); }

bool PanelView::isInterestedInFileDrag (const juce::StringArray& files)   { return profile != nullptr && anyPhoto (files); }
void PanelView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false; repaint();
    for (auto& f : files)
        if (juce::File (f).hasFileExtension (kPhotoTypes)) { if (onPhotoDropped) onPhotoDropped (juce::File (f)); return; }
}

rm::ControlValue PanelView::value (const rm::ControlDef& c) const
{
    return (snap != nullptr && profile != nullptr) ? snap->get (instanceId, c.id) : rm::ControlValue();
}

void PanelView::updateTransform()
{
    if (profile == nullptr) return;
    auto area = getLocalBounds().toFloat();
    scale = juce::jmax (0.05f, juce::jmin (area.getWidth() / profile->w, area.getHeight() / profile->h));
    panel = juce::Rectangle<float> (profile->w * scale, profile->h * scale).withCentre (area.getCentre());
}

juce::Point<float> PanelView::toScreen (float x, float y) const     { return { panel.getX() + x * scale, panel.getY() + y * scale }; }
juce::Point<float> PanelView::centre (const rm::ControlDef& c) const { return toScreen (c.x, c.y); }
float PanelView::radius (const rm::ControlDef& c) const              { return juce::jmax (7.0f, c.r * scale); }

int PanelView::hitTest (juce::Point<float> p) const
{
    if (profile == nullptr) return -1;
    int best = -1; float bestD = 1.0e9f;
    for (int i = 0; i < (int) profile->controls.size(); ++i)
    {
        auto& c = profile->controls[(size_t) i];
        const float d = centre (c).getDistanceFrom (p);
        if (d < radius (c) * 1.12f + 3.0f && d < bestD) { best = i; bestD = d; }
    }
    return best;
}

String PanelView::tipFor (const rm::ControlDef& c) const
{
    String s = c.name;
    if (c.group.isNotEmpty() && ! c.name.containsIgnoreCase (c.group) && c.group != "Switches" && c.group != "Global")
        s = c.group + " " + s;
    s << "   " << rm::describeValue (c, value (c));
    if (c.type == ControlType::stepped && c.options.isEmpty() && c.scale.empty() && c.minLabel.isNotEmpty())
        s << "   (" << c.minLabel << U (" … ") << c.maxLabel << ")";
    return s;
}

void PanelView::sendHover (int index)
{
    if (! onHover) return;
    if (index < 0 || profile == nullptr) { onHover ({}, {}, 0); return; }
    auto& c = profile->controls[(size_t) index];
    const auto p = centre (c);
    onHover (tipFor (c), { juce::roundToInt (p.x), juce::roundToInt (p.y) }, juce::roundToInt (radius (c)));
}

void PanelView::setValueFull (int index, const rm::ControlValue& v)
{
    if (profile == nullptr || snap == nullptr || index < 0) return;
    auto& c = profile->controls[(size_t) index];
    const auto key = rm::valueKey (instanceId, c.id);
    const auto old = snap->get (key);
    if (old.known == v.known && old.pos == v.pos && old.source == v.source) return;
    if (v.known) snap->values[key] = v; else snap->values.erase (key);
    if (entry == nullptr) sendHover (index);
    if (onEdit) onEdit();
    repaint();
}

void PanelView::setValue (int index, bool known, double pos)
{
    if (profile == nullptr || index < 0) return;
    auto& c = profile->controls[(size_t) index];
    rm::ControlValue v;
    v.known = known;
    if (known)
    {
        if (c.type == ControlType::knob) v.pos = juce::jlimit (0.0, 1.0, pos);
        else                             v.pos = (double) juce::jlimit (0, juce::jmax (0, c.numPositions() - 1), (int) std::lround (pos));
    }
    setValueFull (index, v);
}

//==============================================================================
// psani hodnot
std::vector<int> PanelView::readingOrder() const
{
    std::vector<int> idx;
    if (profile == nullptr) return idx;
    for (int i = 0; i < (int) profile->controls.size(); ++i) idx.push_back (i);
    const float band = profile->h > profile->w ? profile->h * 0.10f : profile->h;      // 500 modul: po radcich
    auto& cs = profile->controls;
    std::stable_sort (idx.begin(), idx.end(), [&] (int a, int b)
    {
        const int ra = (int) (cs[(size_t) a].y / band), rb = (int) (cs[(size_t) b].y / band);
        if (ra != rb) return ra < rb;
        return cs[(size_t) a].x < cs[(size_t) b].x;
    });
    return idx;
}

juce::Rectangle<int> PanelView::entryBounds (int index) const
{
    auto& c = profile->controls[(size_t) index];
    const auto p = centre (c);
    const float r = radius (c);
    const int w = juce::jlimit (52, 96, juce::roundToInt (r * 1.9f)), h = 24;
    juce::Rectangle<int> b (w, h);
    b = b.withCentre ({ juce::roundToInt (p.x), juce::roundToInt (c.type == ControlType::button ? p.y + r + h * 0.6f : p.y) });
    return b.constrainedWithin (getLocalBounds());
}

void PanelView::startEntry (int index)
{
    if (profile == nullptr || snap == nullptr || index < 0 || index >= (int) profile->controls.size()) return;
    if (entry != nullptr && ! commitEntry()) return;
    closeEntry();

    entryIndex = index;
    entry = std::make_unique<EntryBox>();
    auto& e = *entry;
    styleEditor (e, "?");
    e.setFont (font (14.5f, true));
    e.setIndents (4, 3);
    e.setJustification (juce::Justification::centred);
    e.setColour (juce::TextEditor::backgroundColourId, Colour (0xf0101114));
    e.setColour (juce::TextEditor::outlineColourId, kAccent);
    e.setColour (juce::TextEditor::focusedOutlineColourId, kAccent);
    const auto v = value (profile->controls[(size_t) index]);
    e.setText (v.known ? rm::entryText (profile->controls[(size_t) index], v).trimCharactersAtStart (U ("≈")) : String(), false);
    e.selectAll();
    e.onReturnKey = [this] { if (commitEntry()) closeEntry(); };
    e.onEscapeKey = [this] { closeEntry(); };
    e.onTab       = [this] (bool forward) { if (commitEntry()) moveEntry (forward); };
    e.onFocusLost = [this]
    {
        if (entryClosing || entry == nullptr) return;
        juce::Component::SafePointer<PanelView> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr || safe->entry == nullptr || safe->entry->hasKeyboardFocus (true)) return;
            if (! safe->commitEntry()) {}      // nepouzitelny text: puvodni hodnota zustava
            safe->closeEntry();
        });
    };
    e.setBounds (entryBounds (index));
    addAndMakeVisible (e);
    e.grabKeyboardFocus();
    if (onHover) onHover ({}, {}, 0);
    repaint();
}

void PanelView::startEntryEdge (bool first)
{
    auto order = readingOrder();
    if (! order.empty()) startEntry (first ? order.front() : order.back());
}

bool PanelView::commitEntry()
{
    if (entry == nullptr || profile == nullptr || entryIndex < 0) return true;
    auto& c = profile->controls[(size_t) entryIndex];
    const auto text = entry->getText();
    const auto old = value (c);
    if (old.known && text.trim() == rm::entryText (c, old).trimCharactersAtStart (U ("≈"))) return true;   // beze zmeny (zachova zdroj z fotky)
    rm::ControlValue v;
    if (! rm::parseEntry (c, text, v))
    {
        entry->setColour (juce::TextEditor::outlineColourId, Colour (0xffff4040));
        entry->setColour (juce::TextEditor::focusedOutlineColourId, Colour (0xffff4040));
        entry->selectAll();
        entry->repaint();
        return false;
    }
    setValueFull (entryIndex, v);
    return true;
}

void PanelView::closeEntry()
{
    if (entry == nullptr) return;
    entryClosing = true;
    removeChildComponent (entry.get());
    entry.reset();
    entryClosing = false;
    entryIndex = -1;
    repaint();
}

void PanelView::moveEntry (bool forward)
{
    auto order = readingOrder();
    auto it = std::find (order.begin(), order.end(), entryIndex);
    if (it == order.end()) { closeEntry(); return; }
    const long pos = (long) (it - order.begin()) + (forward ? 1 : -1);
    if (pos < 0 || pos >= (long) order.size())
    {
        closeEntry();
        if (onEntryLeave) onEntryLeave (this, forward);
        return;
    }
    startEntry (order[(size_t) pos]);
}

void PanelView::resized()
{
    updateTransform();
    if (entry != nullptr && entryIndex >= 0) entry->setBounds (entryBounds (entryIndex));
}

//==============================================================================
void PanelView::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hover)
    {
        hover = h;
        sendHover (h);
        repaint();
    }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
}

void PanelView::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; sendHover (-1); repaint(); }
}

void PanelView::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1; dragMoved = false; grabbing = false;
    const int i = hitTest (e.position);
    if (i < 0)
    {
        // mimo ovladace: chyti se cely pristroj (presun v racku)
        if (! e.mods.isPopupMenu() && onGrab) { grabbing = true; onGrab (e, 0); }
        return;
    }
    if (snap == nullptr) return;
    auto& c = profile->controls[(size_t) i];
    const auto v = value (c);

    if (e.mods.isPopupMenu()) { showMenu (i); return; }

    if (c.type == ControlType::button)
    {
        setValue (i, true, (v.known && v.pos >= 0.5) ? 0.0 : 1.0);
    }
    else if (c.type == ControlType::selector)
    {
        setValue (i, true, v.known ? (double) (((int) v.pos + 1) % juce::jmax (1, c.numPositions())) : 0.0);
    }
    else
    {
        dragging = i;
        dragStartKnown = v.known;
        dragStart = v.known ? v.pos : (c.type == ControlType::knob ? 0.5 : (double) ((c.steps - 1) / 2));
    }
}

void PanelView::mouseDrag (const juce::MouseEvent& e)
{
    if (grabbing) { if (onGrab) onGrab (e, 1); return; }
    if (dragging < 0) return;
    auto& c = profile->controls[(size_t) dragging];
    const double dy = (double) -e.getDistanceFromDragStartY() + (double) e.getDistanceFromDragStartX();
    if (std::abs (dy) >= 3.0) dragMoved = true;
    if (! dragStartKnown && std::abs (dy) < 3.0) return;   // pouhe kliknuti hodnotu nevytvori

    if (c.type == ControlType::knob)
        setValue (dragging, true, dragStart + dy / (e.mods.isShiftDown() ? 800.0 : 220.0));
    else
        setValue (dragging, true, dragStart + std::round (dy / (c.steps > 12 ? 7.0 : 16.0)));
}

void PanelView::mouseUp (const juce::MouseEvent& e)
{
    if (grabbing) { grabbing = false; if (onGrab) onGrab (e, 2); return; }
    if (dragging >= 0)
    {
        const int i = dragging;
        dragging = -1;
        // v rezimu 123 kliknuti (bez tahu) otevre psani cisla
        if (showValues && ! dragMoved && ! e.mods.isPopupMenu()) startEntry (i);
        repaint();
    }
}

void PanelView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = hitTest (e.position);
    if (i >= 0 && profile->controls[(size_t) i].type != ControlType::button) startEntry (i);
}

void PanelView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = hitTest (e.position);
    if (i < 0 || snap == nullptr) return;
    auto& c = profile->controls[(size_t) i];
    if (c.type == ControlType::button || c.type == ControlType::selector) return;
    const auto v = value (c);
    const double delta = (double) (w.isReversed ? -w.deltaY : w.deltaY);

    if (c.type == ControlType::knob)
    {
        setValue (i, true, (v.known ? v.pos : 0.5) + delta * 0.15);
    }
    else
    {
        wheelAccum += delta * 6.0;
        const int steps = (int) wheelAccum;
        if (steps != 0) { wheelAccum -= steps; setValue (i, true, (v.known ? v.pos : (double) ((c.steps - 1) / 2)) + steps); }
    }
}

void PanelView::showMenu (int index)
{
    auto& c = profile->controls[(size_t) index];
    juce::PopupMenu m;
    m.addSectionHeader (c.group + " " + c.name);
    if (c.type != ControlType::button) m.addItem (5, U ("Napsat hodnotu…"));
    m.addSeparator();

    if (c.type == ControlType::knob)
    {
        m.addItem (2, c.minLabel.isNotEmpty() ? c.minLabel : String ("min"));
        m.addItem (3, c.centreLabel.isNotEmpty() ? c.centreLabel : String ("12:00"));
        m.addItem (4, c.maxLabel.isNotEmpty() ? c.maxLabel : String ("max"));
    }
    else
    {
        const int n = c.numPositions();
        const auto v = value (c);
        const int current = v.known ? (int) v.pos : -1;
        for (int i = 0; i < n; ++i)
        {
            String label;
            if (c.type == ControlType::button)        label = i == 1 ? "ON" : "OFF";
            else if (c.options.size() == n)           label = c.options[i];
            else
            {
                rm::ControlValue t; t.known = true; t.pos = i;
                label = String (i + 1);
                const auto txt = rm::entryText (c, t);
                if (txt != label) label << "   (" << txt << ")";
                else if (i == 0 && c.minLabel.isNotEmpty())                              label << "   (" << c.minLabel << ")";
                else if (i == n - 1 && c.maxLabel.isNotEmpty())                          label << "   (" << c.maxLabel << ")";
                else if (n % 2 == 1 && i == n / 2 && c.centreLabel.isNotEmpty())         label << "   (" << c.centreLabel << ")";
            }
            m.addItem (100 + i, label, true, i == current);
        }
    }
    m.addSeparator();
    m.addItem (1, U ("? nezadáno"));

    juce::Component::SafePointer<PanelView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe, index] (int r)
                     {
                         if (safe == nullptr || r == 0) return;
                         if (r == 1)        safe->setValue (index, false, 0.0);
                         else if (r == 2)   safe->setValue (index, true, 0.0);
                         else if (r == 3)   safe->setValue (index, true, 0.5);
                         else if (r == 4)   safe->setValue (index, true, 1.0);
                         else if (r == 5)   safe->startEntry (index);
                         else if (r >= 100) safe->setValue (index, true, (double) (r - 100));
                     });
}

void PanelView::drawControl (juce::Graphics& g, int index)
{
    auto& c = profile->controls[(size_t) index];
    auto& d = c.draw;
    const auto v = value (c);
    const auto p = centre (c);
    const float r = radius (c);
    const bool isHover = index == hover || index == dragging || index == entryIndex;
    const bool rotary = c.type == ControlType::knob || c.type == ControlType::stepped;
    const int pos = v.known ? (int) v.pos : -1;

    const bool overPhoto = showPhoto && photo.isValid();
    if (overPhoto)
    {
        // nad fotkou uzivatele jen tenke znacky, aby bylo videt, jestli sedi se skutecnosti
        const Colour col = v.source == "recognized" ? kFromPhoto : juce::Colours::white;
        if (rotary && v.known && d.kind == "sprite")
        {
            const auto a = (float) juce::degreesToRadians (rm::angleFor (c, v));
            const juce::Point<float> p0 (p.x + std::sin (a) * r * 0.15f, p.y - std::cos (a) * r * 0.15f), p1 (p.x + std::sin (a) * r * 1.08f, p.y - std::cos (a) * r * 1.08f);
            g.setColour (Colour (0xcc000000)); g.drawLine (juce::Line<float> (p0, p1), 4.0f);
            g.setColour (col);                 g.drawLine (juce::Line<float> (p0, p1), 2.0f);
        }
        else if (v.known && (c.type == ControlType::button || c.type == ControlType::selector || d.kind == "ring"))
        {
            const float rr = juce::jmax (5.0f, r * 0.55f);
            g.setColour (Colour (0xcc000000)); g.drawEllipse (p.x - rr, p.y - rr, rr * 2, rr * 2, 3.5f);
            g.setColour (v.pos >= 0.5 || c.type != ControlType::button ? col : col.withAlpha (0.35f));
            g.drawEllipse (p.x - rr, p.y - rr, rr * 2, rr * 2, 1.8f);
        }
    }
    else if (d.kind == "sprite")
    {
        auto it = sprites.find (d.sprite);
        if (it != sprites.end() && it->second.isValid())
        {
            auto& img = it->second;
            // nezadano: knob zustava tak, jak je na fotce
            const double rotation = v.known ? rm::angleFor (c, v) - d.refAngle : 0.0;
            const float k = scale * (2.0f * d.spriteR) / (float) img.getWidth();
            auto t = juce::AffineTransform::translation (-(float) img.getWidth() * 0.5f, -(float) img.getHeight() * 0.5f)
                         .rotated ((float) juce::degreesToRadians (rotation)).scaled (k).translated (p.x, p.y);
            g.setOpacity (v.known ? 1.0f : 0.5f);
            g.drawImageTransformed (img, t);
            g.setOpacity (1.0f);
        }
    }
    else if (d.kind == "ring")
    {
        if (pos >= 0 && pos < (int) d.points.size())
            drawGlow (g, toScreen (d.points[(size_t) pos].x, d.points[(size_t) pos].y), juce::jmax (2.5f, d.ledR * scale), Colour (d.colour));
    }
    else if (d.kind == "leds")
    {
        if (pos >= 0 && pos < (int) d.map.size())
        {
            const int led = d.map[(size_t) pos];
            if (led >= 0 && led < (int) d.points.size())
                drawGlow (g, toScreen (d.points[(size_t) led].x, d.points[(size_t) led].y), juce::jmax (2.5f, d.ledR * scale * 0.85f), Colour (d.colour));
        }
    }
    else if (d.kind == "toggle")
    {
        if (v.known && pos != d.photoState && background.isValid())
        {
            const juce::Rectangle<int> src ((int) (c.x - d.w * 0.5f), (int) (c.y - d.h * 0.5f), (int) d.w, (int) d.h);
            auto part = background.getClippedImage (src.getIntersection (background.getBounds()));
            const bool vertical = d.axis != "h";
            auto t = juce::AffineTransform::translation (-(float) part.getWidth() * 0.5f, -(float) part.getHeight() * 0.5f)
                         .scaled (vertical ? 1.0f : -1.0f, vertical ? -1.0f : 1.0f).scaled (scale).translated (p.x, p.y);
            g.drawImageTransformed (part, t);
        }
    }
    else if (d.kind == "glow")
    {
        const bool on = v.known && v.pos >= 0.5;
        const Colour col (d.colour);
        const auto gp = toScreen (d.gx, d.gy);
        if (d.shape == "rect")
        {
            juce::Rectangle<float> rc (d.gw * scale, d.gh * scale);
            rc = rc.withCentre (gp);
            if (v.known && ! on)            { g.setColour (Colour (0xe6101010)); g.fillRoundedRectangle (rc, 2.0f); }
            else if (on && ! d.litInPhoto)  { g.setColour (col.withAlpha (0.85f)); g.fillRoundedRectangle (rc, 2.0f); }
        }
        else if (on) // ring
        {
            const float rr = d.gr * scale, w = juce::jmax (2.0f, d.gw * scale);
            g.setColour (col.withAlpha (0.25f)); g.drawEllipse (gp.x - rr, gp.y - rr, rr * 2, rr * 2, w * 2.6f);
            g.setColour (col.withAlpha (0.55f)); g.drawEllipse (gp.x - rr, gp.y - rr, rr * 2, rr * 2, w * 1.6f);
            g.setColour (col.brighter (0.3f));   g.drawEllipse (gp.x - rr, gp.y - rr, rr * 2, rr * 2, w);
        }
    }

    if (! v.known)
    {
        // nenapadna znacka "nezadano" (po fotce: tady si plugin nebyl jisty)
        const float dr = juce::jlimit (2.5f, 4.5f, r * 0.16f);
        g.setColour (Colour (0xff000000)); g.fillEllipse (p.x - dr - 1.2f, p.y - dr - 1.2f, (dr + 1.2f) * 2, (dr + 1.2f) * 2);
        g.setColour (kUnknown);            g.fillEllipse (p.x - dr, p.y - dr, dr * 2, dr * 2);
    }
    else if (v.source == "recognized" && ! overPhoto)
    {
        // precteno z fotky, jeste nezkontrolovano rukou
        const float dr = juce::jlimit (2.0f, 3.5f, r * 0.12f);
        const juce::Point<float> m (p.x + r * 0.78f, p.y - r * 0.78f);
        g.setColour (Colour (0xff000000)); g.fillEllipse (m.x - dr - 1.0f, m.y - dr - 1.0f, (dr + 1.0f) * 2, (dr + 1.0f) * 2);
        g.setColour (kFromPhoto);          g.fillEllipse (m.x - dr, m.y - dr, dr * 2, dr * 2);
    }

    if (isHover)
    {
        g.setColour (kAccent.withAlpha (0.95f));
        g.drawEllipse (p.x - r - 2, p.y - r - 2, (r + 2) * 2, (r + 2) * 2, 1.6f);
    }

    if (showValues && v.known && c.type != ControlType::button && index != entryIndex)
    {
        const auto text = rm::shortValue (c, v);
        auto f = font (juce::jlimit (9.5f, 13.0f, r * 0.62f), true);
        g.setFont (f);
        const float w = juce::GlyphArrangement::getStringWidth (f, text) + 8.0f, h = f.getHeight() + 2.0f;
        auto pill = juce::Rectangle<float> (w, h).withCentre (rotary ? p : juce::Point<float> (p.x, p.y + r + h * 0.55f));
        g.setColour (Colour (0xd8000000)); g.fillRoundedRectangle (pill, 3.5f);
        g.setColour (v.source == "recognized" ? kFromPhoto.brighter (0.4f) : juce::Colours::white);
        g.drawText (text, pill, juce::Justification::centred);
    }
}

void PanelView::paint (juce::Graphics& g)
{
    if (profile == nullptr) return;

    updateTransform();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    if (showPhoto && photo.isValid())
    {
        g.drawImage (photo, panel);
    }
    else if (background.isValid())
    {
        // predskalovane pozadi: kresli se pri kazdem pohybu knobu, tak at je to levne
        const float px = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
        const int tw = juce::jmax (1, juce::roundToInt (panel.getWidth() * px)), th = juce::jmax (1, juce::roundToInt (panel.getHeight() * px));
        if (! scaledBackground.isValid() || scaledBackground.getWidth() != tw || scaledBackground.getHeight() != th)
            scaledBackground = tw >= background.getWidth() ? background
                                                           : background.rescaled (tw, th, juce::Graphics::highResamplingQuality);
        g.drawImage (scaledBackground, panel);
    }
    else
    {
        g.setColour (Colour (0xff1c2026)); g.fillRect (panel);
        g.setColour (kDim); g.setFont (font (14.0f));
        g.drawText (profile->name, panel, juce::Justification::centred);
    }

    for (int i = 0; i < (int) profile->controls.size(); ++i)
        drawControl (g, i);

    if (dragOver)
    {
        g.setColour (kFromPhoto.withAlpha (0.22f)); g.fillRect (panel);
        g.setColour (kFromPhoto); g.drawRect (panel, 3.0f);
    }
}

//==============================================================================
// PhotoSlot
//==============================================================================
bool PhotoSlot::isInterestedInFileDrag (const juce::StringArray& files)   { return anyPhoto (files); }
void PhotoSlot::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false; repaint();
    for (auto& f : files)
        if (juce::File (f).hasFileExtension (kPhotoTypes)) { if (onPhoto) onPhoto (juce::File (f)); return; }
}

void PhotoSlot::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown()) return;
    if (onRemove && ! isAdd && removeArea().contains (e.position)) { onRemove(); return; }
    if (onView && ! thumb.isEmpty() && thumb.contains (e.position)) { onView(); return; }
    if (onClick) onClick();
}

void PhotoSlot::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.5f);
    if (isAdd)
    {
        g.setColour (dragOver ? kFromPhoto.withAlpha (0.25f) : hover ? Colour (0xff20242b) : Colour (0xff15171b));
        g.fillRoundedRectangle (b, 6.0f);
        juce::Path outline; outline.addRoundedRectangle (b, 6.0f);
        juce::Path dashed; const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (hover ? kAccent : Colour (0xff4a515d)); g.fillPath (dashed);
        auto a = b.reduced (6.0f);
        g.setColour (hover ? kAccent : kText); g.setFont (font (26.0f, true));
        g.drawText ("+", a.removeFromTop (a.getHeight() * 0.6f), juce::Justification::centred);
        g.setColour (addHint.isNotEmpty() ? kAccent : kDim); g.setFont (font (12.0f));
        g.drawFittedText (addHint.isNotEmpty() ? addHint : U ("přístroj"), a.toNearestInt(), juce::Justification::centredTop, 2);
        return;
    }
    const bool has = state != nullptr && state->rectified.isValid();
    const bool busy = state != nullptr && state->busy;
    const bool failed = state != nullptr && state->failed;
    bool supported = false;
    if (profile != nullptr) for (auto& c : profile->controls) supported = supported || c.read.isNotEmpty();

    g.setColour (dragOver ? kFromPhoto.withAlpha (0.25f) : hover ? Colour (0xff20242b) : Colour (0xff15171b));
    g.fillRoundedRectangle (b, 6.0f);

    juce::Path outline; outline.addRoundedRectangle (b, 6.0f);
    if (has) { g.setColour (kFromPhoto.withAlpha (0.8f)); g.strokePath (outline, juce::PathStrokeType (1.4f)); }
    else
    {
        juce::Path dashed; const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (dragOver ? kFromPhoto : failed ? kAccent : Colour (0xff4a515d)); g.fillPath (dashed);
    }

    auto area = b.reduced (8.0f, 6.0f);
    thumb = {};
    const auto& shown = state != nullptr && state->original.isValid() ? state->original : (state != nullptr ? state->rectified : juce::Image());
    if (shown.isValid())
    {
        // nahled nahrane fotky vlevo (klik = zvetsit)
        const float ih = area.getHeight(), iw = juce::jmin (area.getWidth() * 0.45f, ih * (float) shown.getWidth() / (float) shown.getHeight());
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        thumb = area.removeFromLeft (iw);
        g.drawImage (shown, thumb, juce::RectanglePlacement::centred);
        area.removeFromLeft (8.0f);
    }

    String name = title.isNotEmpty() ? title : (profile != nullptr ? profile->name : String());
    if (name.startsWith ("elysia ") || name.startsWith ("SPL ") || name.startsWith ("WesAudio ")) name = name.fromFirstOccurrenceOf (" ", false, false);
    g.setColour (kText); g.setFont (font (13.5f, true));
    g.drawFittedText (name, area.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, 1);

    String line;
    Colour lc = kDim;
    if (busy)               { line = U ("čtu fotku…"); lc = kFromPhoto; }
    else if (failed)        { line = state->status; lc = kAccent; }
    else if (has)           { line = U ("z fotky ") + String (state->read) + "/" + String (state->readable); lc = kFromPhoto; }
    else if (! supported)   { line = U ("fotka se jen uloží"); }
    else                    { line = U ("+ fotka"); }
    g.setColour (lc); g.setFont (font (12.5f));
    g.drawFittedText (line, area.toNearestInt(), juce::Justification::topLeft, 3);

    if (hover && onRemove)
    {
        // odebrat z racku (definice v knihovne i ulozena nastaveni zustanou)
        auto r = removeArea();
        g.setColour (overRemove ? Colour (0xffd04040) : Colour (0xff2a2e35)); g.fillEllipse (r);
        g.setColour (kText);
        const auto c = r.getCentre(); const float k = 4.5f;
        g.drawLine (c.x - k, c.y - k, c.x + k, c.y + k, 1.6f); g.drawLine (c.x - k, c.y + k, c.x + k, c.y - k, 1.6f);
    }
}


//==============================================================================
// PhotoViewer
//==============================================================================
void PhotoViewer::paint (juce::Graphics& g)
{
    g.fillAll (Colour (0xf20b0c0e));
    auto b = getLocalBounds().reduced (16);
    g.setColour (kText); g.setFont (font (14.0f, true));
    g.drawText (caption + U ("     (klikni pro zavření)"), b.removeFromTop (24), juce::Justification::centredLeft);
    if (image.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (image, b.toFloat(), juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    }
}

//==============================================================================
// LevelStrip
//==============================================================================
namespace
{
const char* const kGainTip = "Hlasitost v tomto místě řetězce (0 dB = signál beze změny). Ukládá se k verzi.";
void styleGain (juce::Slider& sl)
{
    sl.setSliderStyle (juce::Slider::LinearBar);
    sl.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 22);
    sl.setTextValueSuffix (" dB");
    sl.setNumDecimalPlacesToDisplay (1);
    sl.setRange (-24.0, 24.0, 0.1);
    sl.setDoubleClickReturnValue (true, 0.0);
    sl.setColour (juce::Slider::trackColourId, Colour (0xff3a4250));
    sl.setColour (juce::Slider::backgroundColourId, kField);
    sl.setColour (juce::Slider::textBoxTextColourId, kText);
    sl.setColour (juce::Slider::textBoxOutlineColourId, Colour (0x00000000));
    sl.setTooltip (U (kGainTip));
}
}

LevelStrip::LevelStrip (RackMemoryProcessor& p) : proc (p)
{
    roleBox.addItem (U ("Úroveň"), 1);
    roleBox.addItem (U ("IN – před HW"), 2);
    roleBox.addItem (U ("OUT – za HW"), 3);
    roleBox.setTooltip (U ("Dvě instance: jedna před hardwarem (IN), druhá za ním (OUT). Pak ukazují obě úrovně a rozdíl."));
    for (int g = 1; g <= 4; ++g) groupBox.addItem (U ("řetěz ") + String (g), g);
    for (auto* b : { &roleBox, &groupBox })
    {
        b->setColour (juce::ComboBox::backgroundColourId, kField);
        b->setColour (juce::ComboBox::textColourId, kText);
        b->setColour (juce::ComboBox::outlineColourId, Colour (0x00000000));
        addAndMakeVisible (*b);
    }
    roleBox.onChange = [this] { proc.setRole ((RackMemoryProcessor::Role) (roleBox.getSelectedId() - 1), groupBox.getSelectedId()); refresh(); };
    groupBox.onChange = [this] { proc.setRole (proc.role, groupBox.getSelectedId()); refresh(); };

    styleGain (ownGain); styleGain (otherGain);
    ownAttach = std::make_unique<juce::SliderParameterAttachment> (*proc.gainParam, ownGain);
    otherGain.onValueChange = [this] { if (auto* o = proc.partner()) o->setGainDb ((float) otherGain.getValue()); };
    addAndMakeVisible (ownGain); addChildComponent (otherGain);

    styleButton (matchButton, U ("Srovnat"));
    matchButton.setTooltip (U ("Nastaví OUT tak, aby byl stejně hlasitý jako IN (férové A/B: s hardwarem vs. bez). Měří se od posledního Reset."));
    styleButton (resetButton, U ("Reset"));
    resetButton.setTooltip (U ("Začít měřit znovu (integrovaná hlasitost a maximum)"));
    matchButton.onClick = [this]
    {
        auto* o = proc.partner();
        if (o == nullptr) return;
        auto* in  = proc.role == RackMemoryProcessor::Role::in ? &proc : o;
        auto* out = proc.role == RackMemoryProcessor::Role::in ? o : &proc;
        double si = 0, so = 0;
        float li = in->meter.integratedLufs (&si), lo = out->meter.integratedLufs (&so);
        if (si < 3.0 || so < 3.0) { li = in->meter.momentaryLufs(); lo = out->meter.momentaryLufs(); }
        if (li < -69.0f || lo < -69.0f) return;                   // ticho - neni co srovnat
        out->setGainDb (out->gainDb() + (li - lo));
        in->meter.reset(); out->meter.reset();
    };
    resetButton.onClick = [this] { proc.meter.reset(); if (auto* o = proc.partner()) o->meter.reset(); };
    addAndMakeVisible (resetButton); addChildComponent (matchButton);
    refresh();
    startTimerHz (25);
}

LevelStrip::~LevelStrip()   { stopTimer(); }

void LevelStrip::refresh()
{
    roleBox.setSelectedId ((int) proc.role + 1, juce::dontSendNotification);
    groupBox.setSelectedId (proc.linkGroup, juce::dontSendNotification);
    groupBox.setVisible (proc.role != RackMemoryProcessor::Role::none);
    resized();
    repaint();
}

void LevelStrip::resized()
{
    auto b = getLocalBounds().reduced (8, 4);
    roleBox.setBounds (b.removeFromLeft (122)); b.removeFromLeft (6);
    if (groupBox.isVisible()) { groupBox.setBounds (b.removeFromLeft (84)); b.removeFromLeft (6); }
    resetButton.setBounds (b.removeFromRight (60)); b.removeFromRight (6);

    const bool pair = proc.role != RackMemoryProcessor::Role::none;
    matchButton.setVisible (pair && proc.partner() != nullptr);
    if (matchButton.isVisible()) { matchButton.setBounds (b.removeFromRight (70)); b.removeFromRight (6); }
    deltaArea = pair ? b.removeFromRight (104) : juce::Rectangle<int>();
    if (pair) b.removeFromRight (6);

    auto& gA = proc.role == RackMemoryProcessor::Role::out ? otherGain : ownGain;
    auto& gB = proc.role == RackMemoryProcessor::Role::out ? ownGain : otherGain;
    otherGain.setVisible (pair);
    if (! pair)
    {
        ownGain.setBounds (b.removeFromRight (140)); b.removeFromRight (6);
        meterA = b; meterB = {};
        return;
    }
    auto half = b.removeFromLeft (b.getWidth() / 2);
    gA.setBounds (half.removeFromRight (100)); half.removeFromRight (6);
    meterA = half;
    b.removeFromLeft (8);
    gB.setBounds (b.removeFromRight (100)); b.removeFromRight (6);
    meterB = b;
}

void LevelStrip::timerCallback()
{
    // druha instance se muze objevit / zmizet kdykoli (vlozena na jinou pozici v Logicu)
    auto* o = proc.partner();
    const bool shouldMatch = proc.role != RackMemoryProcessor::Role::none && o != nullptr;
    if (shouldMatch != matchButton.isVisible()) resized();
    otherGain.setEnabled (o != nullptr);
    if (o != nullptr && ! otherGain.isMouseButtonDown()) otherGain.setValue (o->gainDb(), juce::dontSendNotification);
    repaint (meterA.getUnion (meterB).getUnion (deltaArea));
}

String LevelStrip::deltaText() const
{
    auto* o = proc.partner();
    if (proc.role == RackMemoryProcessor::Role::none || o == nullptr) return {};
    auto* in  = proc.role == RackMemoryProcessor::Role::in ? &proc : o;
    auto* out = proc.role == RackMemoryProcessor::Role::in ? o : &proc;
    double si = 0, so = 0;
    float li = in->meter.integratedLufs (&si), lo = out->meter.integratedLufs (&so);
    if (si < 3.0 || so < 3.0) { li = in->meter.momentaryLufs(); lo = out->meter.momentaryLufs(); }
    if (li < -69.0f || lo < -69.0f) return U ("HW: –");
    const float hw = (lo - out->gainDb()) - li;                 // co pridava / ubira samotny hardware
    return U ("HW: ") + (hw >= 0 ? "+" : "") + String (hw, 1) + " dB";
}

void LevelStrip::drawMeter (juce::Graphics& g, juce::Rectangle<int> r, const String& label, const RackMemoryProcessor* p, bool present)
{
    if (r.isEmpty()) return;
    auto b = r.toFloat();
    g.setColour (Colour (0xff111316)); g.fillRoundedRectangle (b, 4.0f);
    auto text = b.reduced (6.0f, 0.0f);
    g.setFont (font (12.5f, true)); g.setColour (kDim);
    g.drawText (label, text.removeFromLeft (label.length() > 2 ? 30.0f : 24.0f), juce::Justification::centredLeft);
    if (! present || p == nullptr)
    {
        g.setColour (kDim); g.setFont (font (12.0f));
        g.drawText (label == "OUT" ? U ("vlož druhý Rack Memory za HW a přepni ho na OUT") : U ("vlož druhý Rack Memory před HW a přepni ho na IN"),
                    text, juce::Justification::centredLeft);
        return;
    }
    const float m = p->meter.momentaryLufs(), pk = p->meter.maxPeakDb();
    double secs = 0; const float integ = p->meter.integratedLufs (&secs);
    auto nums = text.removeFromRight (84.0f);
    auto bar = text.reduced (0.0f, 8.0f);
    auto pos = [&bar] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    g.setColour (Colour (0xff23272e)); g.fillRoundedRectangle (bar, 2.0f);
    if (m > -60.0f)
    {
        const Colour c = m > -9.0f ? Colour (0xffe0533a) : m > -16.0f ? kAccent : kOk;
        g.setColour (c); g.fillRoundedRectangle (bar.withRight (pos (m)), 2.0f);
    }
    if (pk > -60.0f) { g.setColour (pk > -0.3f ? Colour (0xffff4040) : kText); g.fillRect (juce::Rectangle<float> (pos (pk) - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f)); }
    g.setFont (font (12.0f)); g.setColour (kText);
    juce::ignoreUnused (secs, integ);
    g.drawText ((m > -69.0f ? String (m, 1) : U ("–")) + " LUFS", nums, juce::Justification::centredRight);
}

void LevelStrip::paint (juce::Graphics& g)
{
    g.fillAll (Colour (0xff121417));
    using R = RackMemoryProcessor::Role;
    auto* o = proc.partner();
    if (proc.role == R::none) drawMeter (g, meterA, U ("LVL"), &proc, true);
    else
    {
        drawMeter (g, meterA, "IN",  proc.role == R::in ? &proc : o, proc.role == R::in || o != nullptr);
        drawMeter (g, meterB, "OUT", proc.role == R::out ? &proc : o, proc.role == R::out || o != nullptr);
        if (! deltaArea.isEmpty())
        {
            g.setColour (kText); g.setFont (font (13.0f, true));
            g.drawText (deltaText(), deltaArea, juce::Justification::centred);
        }
    }
}

//==============================================================================
// Editor
//==============================================================================
RackMemoryEditor::RackMemoryEditor (RackMemoryProcessor& p)
    : AudioProcessorEditor (&p), proc (p), levels (p)
{
    setOpaque (true);

    styleEditor (songEdit, U ("Skladba"));
    styleEditor (noteEdit, U ("Poznámka"));
    addAndMakeVisible (songEdit);
    addAndMakeVisible (noteEdit);
    songEdit.onTextChange = [this]
    {
        if (updating) return;
        proc.songName = songEdit.getText().trim();
        proc.commitState();
        rebuildSnapshotBox();
    };
    noteEdit.onTextChange = [this] { if (! updating) { proc.working.notes = noteEdit.getText(); edited(); } };

    snapshotBox.setTextWhenNothingSelected (U ("Načíst…"));
    snapshotBox.setTextWhenNoChoicesAvailable (U ("Zatím nic uloženého"));
    snapshotBox.setColour (juce::ComboBox::backgroundColourId, kField);
    snapshotBox.setColour (juce::ComboBox::textColourId, kText);
    snapshotBox.setColour (juce::ComboBox::outlineColourId, Colour (0x00000000));
    snapshotBox.onChange = [this]
    {
        if (updating) return;
        const int idx = snapshotBox.getSelectedId() - 1;
        if (idx >= 0 && idx < (int) proc.all.size())
        {
            const auto id = proc.all[(size_t) idx].id;      // kopie: load() meni proc.all az pri dalsim reloadu
            proc.load (id);
            refreshAll();
        }
    };
    addAndMakeVisible (snapshotBox);

    styleButton (saveButton, U ("ULOŽIT"));
    styleButton (undoButton, U ("Zpět"));
    styleButton (updateButton, U ("AKTUALIZOVAT"));
    updateButton.setColour (juce::TextButton::buttonColourId, kOk);
    styleButton (valuesButton, "123");
    styleButton (menuButton, U ("•••"));
    valuesButton.setClickingTogglesState (true);
    valuesButton.setTooltip (U ("Hodnoty na panelech; kliknutím na knob se dá číslo napsat"));
    for (auto* b : { &saveButton, &undoButton, &valuesButton, &menuButton })
        addAndMakeVisible (*b);
    addChildComponent (updateButton);

    saveButton.onClick   = [this] { doSave(); };
    undoButton.onClick   = [this] { proc.undo(); refreshAll(); };
    updateButton.onClick = [this] { startUpdate(); };
    valuesButton.onClick = [this]
    {
        proc.showValues = valuesButton.getToggleState();
        proc.commitState();
        for (auto& kv : panels) kv.second->setShowValues (proc.showValues);
    };
    menuButton.onClick   = [this] { showMenu(); };

    addChildComponent (viewer);
    addAndMakeVisible (levels);
    addTile.isAdd = true;
    addTile.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addTile);

    tip.setFont (font (14.0f, true));
    tip.setColour (juce::Label::textColourId, juce::Colours::white);
    tip.setColour (juce::Label::backgroundColourId, Colour (0xf0101114));
    tip.setColour (juce::Label::outlineColourId, kAccent.withAlpha (0.8f));
    tip.setJustificationType (juce::Justification::centred);
    tip.setInterceptsMouseClicks (false, false);
    addChildComponent (tip);

    proc.addChangeListener (this);
    proc.reloadLibrary();
    librarySignature = proc.library.signature();
    syncComponents();

    // vychozi velikost: cely rack na jednu obrazovku
    int availH = 900;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        availH = d->userArea.getHeight() - 110;
    int w = 1120;
    auto fullHeight = [this] (int width) { return rackHeight (width - 2 * kGap, 0) + kBarHeight + kLevelHeight + kStripHeight + 3 * kGap; };
    while (w > 760 && fullHeight (w) > availH) w -= 20;
    setResizable (true, true);
    setResizeLimits (720, 480, 2600, 1800);
    setSize (w, juce::jmin (availH, fullHeight (w)));

    refreshAll();
    checkForUpdate();
    updater->maybeAutoCheck();          // GitHub: nejvys jednou za 6 hodin, na pozadi
    startTimer (500);
}

RackMemoryEditor::~RackMemoryEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
}

PanelView* RackMemoryEditor::panelFor (const String& id) const
{
    auto it = panels.find (id);
    return it != panels.end() ? it->second.get() : nullptr;
}

void RackMemoryEditor::syncComponents()
{
    order = proc.rackOrder();

    for (auto it = panels.begin(); it != panels.end();)
        if (! order.contains (it->first)) it = panels.erase (it); else ++it;
    for (auto it = slots.begin(); it != slots.end();)
        if (! order.contains (it->first)) it = slots.erase (it); else ++it;

    for (auto& id : order)
    {
        if (panels.find (id) == panels.end())
        {
            auto pv = std::make_unique<PanelView>();
            auto* raw = pv.get();
            const String devId (id);
            pv->onEdit  = [this] { edited(); };
            pv->onHover = [this, raw] (const String& t, juce::Point<int> where, int radius) { showTip (*raw, t, where, radius); };
            pv->onPhotoDropped = [this, devId] (const juce::File& f) { importPhoto (f, devId); };
            pv->onGrab = [this, devId] (const juce::MouseEvent& e, int phase) { handleGrab (devId, e, phase); };
            pv->onEntryLeave = [this] (PanelView* from, bool forward) { entryLeave (from, forward); };
            addAndMakeVisible (*pv);
            panels[id] = std::move (pv);
        }
        if (slots.find (id) == slots.end())
        {
            auto slot = std::make_unique<PhotoSlot>();
            const String devId (id);
            slot->profile = rm::profileForInstance (id);
            slot->onPhoto = [this, devId] (const juce::File& f) { importPhoto (f, devId); };
            slot->onClick = [this, devId] { choosePhoto (devId); };
            slot->onRemove = [this, devId] { removeDevice (devId); };
            slot->onView = [this, devId]
            {
                auto it = proc.devicePhotos.find (devId);
                if (it == proc.devicePhotos.end()) return;
                viewer.image = it->second.original.isValid() ? it->second.original : it->second.rectified;
                viewer.caption = proc.rack.displayName (devId) + "   " + it->second.status;
                viewer.setBounds (getLocalBounds().withTrimmedTop (kBarHeight));
                viewer.setVisible (true);
                viewer.toFront (false);
            };
            addAndMakeVisible (*slot);
            slots[id] = std::move (slot);
        }
    }
    for (auto& kv : slots) kv.second->title = proc.rack.displayName (kv.first);
    tip.toFront (false);
}

void RackMemoryEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kBar);
    g.fillRect (0, 0, getWidth(), kBarHeight);

    // 500 lunchbox: sasi, kolejnice se srouby, prazdne sloty zaslepene
    for (auto& lb : lunchboxes)
    {
        auto b = lb.box;
        juce::ColourGradient body (Colour (0xff3a3d43), b.getX(), b.getY(), Colour (0xff222428), b.getX(), b.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (b, lb.wall * 0.6f);
        g.setColour (Colour (0xff55595f));
        g.drawRoundedRectangle (b.reduced (0.5f), lb.wall * 0.6f, 1.0f);

        const auto inner = b.reduced (lb.wall, lb.rail);
        for (auto railY : { b.getY() + lb.rail * 0.5f, b.getBottom() - lb.rail * 0.5f })
        {
            g.setColour (Colour (0xff2c2e33));
            g.fillRect (juce::Rectangle<float> (inner.getX(), railY - lb.rail * 0.22f, inner.getWidth(), lb.rail * 0.44f));
            for (int s = 0; s < lb.slots; ++s)
            {
                const float cx = inner.getX() + lb.slotW * (s + 0.5f);
                const float rr = juce::jmax (1.5f, lb.rail * 0.13f);
                g.setColour (Colour (0xff111214)); g.fillEllipse (cx - rr, railY - rr, rr * 2, rr * 2);
            }
        }
        for (int s = 0; s < lb.slots; ++s)
        {
            if (std::find (lb.used.begin(), lb.used.end(), s) != lb.used.end()) continue;
            auto plate = juce::Rectangle<float> (inner.getX() + lb.slotW * s, inner.getY(), lb.slotW, inner.getHeight()).reduced (1.0f, 0.0f);
            juce::ColourGradient pg (Colour (0xff1d1f23), plate.getX(), plate.getY(), Colour (0xff141518), plate.getRight(), plate.getBottom(), false);
            g.setGradientFill (pg);
            g.fillRect (plate);
            g.setColour (Colour (0xff2e3136)); g.drawRect (plate, 1.0f);
            const float sr = juce::jmax (2.0f, lb.slotW * 0.07f);
            for (auto sy : { plate.getY() + plate.getHeight() * 0.035f, plate.getBottom() - plate.getHeight() * 0.035f })
            {
                g.setColour (Colour (0xff8a8f96)); g.fillEllipse (plate.getCentreX() - sr, sy - sr, sr * 2, sr * 2);
                g.setColour (Colour (0xff3a3d42)); g.fillEllipse (plate.getCentreX() - sr * 0.45f, sy - sr * 0.45f, sr * 0.9f, sr * 0.9f);
            }
        }
        g.setColour (Colour (0xff6c7179));
        g.setFont (font (juce::jlimit (8.0f, 11.0f, lb.rail * 0.42f), true));
        g.drawText ("500", juce::Rectangle<float> (b.getX() + lb.wall, b.getBottom() - lb.rail, 40.0f, lb.rail), juce::Justification::centredLeft);
    }
}

void RackMemoryEditor::paintOverChildren (juce::Graphics& g)
{
    if (! grabActive) return;
    if (! dropMarker.isEmpty())
    {
        const Colour c = dropValid ? kAccent : Colour (0xffff4040);
        if (dropSlot >= 0)
        {
            g.setColour (c.withAlpha (0.25f)); g.fillRect (dropMarker);
            g.setColour (c);                   g.drawRect (dropMarker, 2);
        }
        else { g.setColour (c); g.fillRoundedRectangle (dropMarker.toFloat(), 2.0f); }
    }
    if (grabImage.isValid())
    {
        g.setOpacity (0.55f);
        g.drawImageAt (grabImage, grabNow.x - grabOffset.x, grabNow.y - grabOffset.y);
        g.setOpacity (1.0f);
    }
}

// Rozvrzeni podle racku uzivatele: 19" pristroje pres celou sirku v poradi radku, 500 moduly v lunchboxu
// na svych slotech (lunchbox stoji v radach tam, kde je prvni 500 modul).
RackMemoryEditor::Layout RackMemoryEditor::computeLayout (int width, int availableHeight) const
{
    Layout L;
    const auto& rack = proc.rack;
    struct Group { bool box = false; juce::String id; };
    std::vector<Group> groups;
    bool haveBox = false;
    float aspect = 0.2855f;          // sirka jednoho 500 slotu / vyska modulu
    for (auto& it : rack.items)
    {
        auto* p = rm::findProfile (it.model);
        if (p == nullptr) continue;
        if (is500 (p))
        {
            aspect = (p->w / p->h) / (float) juce::jmax (1, slotsOf (p));
            if (! haveBox) { haveBox = true; groups.push_back ({ true, {} }); }
        }
        else groups.push_back ({ false, it.id });
    }

    const float W = (float) width;
    float uH = 0;
    for (auto& gr : groups)
        if (! gr.box) { auto* p = rm::profileForInstance (gr.id); uH += W * p->h / p->w; }
    const int nBoxes = haveBox ? 1 : 0;
    const int boxSlots = rack.lunchboxSlots;
    const float gaps = (float) (kGap * juce::jmax (0, (int) groups.size() - 1));

    // vyska modulu: co dovoli sirka, a (je-li dana) co zbyva na vysku
    const float railK = 0.075f, wallK = 0.045f;
    // lunchbox ma stejnou sirku jako 19" pristroje (moduly se nedeformuji: vyska plyne ze sirky)
    juce::ignoreUnused (availableHeight, uH, gaps);
    float Hm = W / ((float) boxSlots * aspect + 2.0f * wallK);
    Hm = juce::jmax (Hm, 40.0f);
    L.moduleH = nBoxes > 0 ? Hm : 0.0f;

    float y = 0;
    for (auto& gr : groups)
    {
        if (! gr.box)
        {
            auto* p = rm::profileForInstance (gr.id);
            const float h = W * p->h / p->w;
            L.dev[gr.id] = { 0.0f, y, W, h };
            L.rows.push_back ({ gr.id, { 0.0f, y, W, h } });
            y += h + (float) kGap;
            continue;
        }
        Lunchbox lb;
        lb.slots = boxSlots; lb.slotW = Hm * aspect; lb.wall = Hm * wallK; lb.rail = Hm * railK;
        const float bw = W, bh = Hm + 2.0f * lb.rail;
        lb.wall = (W - lb.slotW * (float) lb.slots) * 0.5f;          // presne do sirky racku
        lb.box = { 0.0f, y, bw, bh };
        for (auto& it : rack.items)
        {
            const int n = rack.width500 (it);
            if (n == 0 || it.slot < 0) continue;
            L.dev[it.id] = { lb.box.getX() + lb.wall + lb.slotW * (float) it.slot, y + lb.rail, lb.slotW * (float) n, Hm };
            for (int k = 0; k < n; ++k) lb.used.push_back (it.slot + k);
        }
        L.boxes.push_back (lb);
        L.rows.push_back ({ String(), lb.box });
        y += bh + (float) kGap;
    }
    L.height = juce::roundToInt (juce::jmax (0.0f, y - (float) kGap));
    return L;
}

int RackMemoryEditor::rackHeight (int width, int availableHeight) const
{
    return computeLayout (width, availableHeight).height;
}

void RackMemoryEditor::layoutRack (juce::Rectangle<int> area)
{
    // nejvetsi sirka, pri ktere se cely rack vejde a lunchbox neni vedle 19" pristroju prilis maly
    int w = area.getWidth();
    Layout L;
    for (int i = 0; i < 40; ++i)
    {
        L = computeLayout (w, area.getHeight());
        if (L.height <= area.getHeight()) break;
        const int nw = juce::jmax (300, (int) ((float) w * 0.96f));
        if (nw == w) break;
        w = nw;
    }

    rackArea = juce::Rectangle<int> (w, L.height).withCentre (area.getCentre()).withY (area.getY());
    const float ox = (float) rackArea.getX(), oy = (float) rackArea.getY();
    for (auto& id : order)
    {
        auto it = L.dev.find (id);
        auto* pv = panelFor (id);
        if (it == L.dev.end() || pv == nullptr) continue;
        pv->setBounds (it->second.translated (ox, oy).toNearestInt());
        pv->setData (rm::profileForInstance (id), &proc.working, id);
    }
    lunchboxes = L.boxes;
    for (auto& lb : lunchboxes) lb.box = lb.box.translated (ox, oy);
    rows = L.rows;
    for (auto& r : rows) r.second = r.second.translated (ox, oy);

    // poradi na obrazovce (shora dolu, v lunchboxu zleva doprava) - pro policka na fotky a Tab
    juce::StringArray visual;
    for (auto& r : rows)
    {
        if (r.first.isNotEmpty()) { visual.add (r.first); continue; }
        std::vector<std::pair<int, String>> mods;
        for (auto& it : proc.rack.items) if (proc.rack.width500 (it) > 0) mods.push_back ({ it.slot, it.id });
        std::sort (mods.begin(), mods.end());
        for (auto& m : mods) visual.add (m.second);
    }
    order = visual;
}

void RackMemoryEditor::resized()
{
    auto bar = getLocalBounds().removeFromTop (kBarHeight).reduced (8, 7);
    menuButton.setBounds (bar.removeFromRight (36));   bar.removeFromRight (6);
    valuesButton.setBounds (bar.removeFromRight (46)); bar.removeFromRight (6);
    if (updateButton.isVisible()) { updateButton.setBounds (bar.removeFromRight (150)); bar.removeFromRight (6); }
    saveButton.setBounds (bar.removeFromRight (120));  bar.removeFromRight (6);
    if (undoButton.isVisible()) { undoButton.setBounds (bar.removeFromRight (60)); bar.removeFromRight (6); }

    const int flexible = bar.getWidth();
    songEdit.setBounds (bar.removeFromLeft (juce::jlimit (110, 240, flexible * 24 / 100))); bar.removeFromLeft (6);
    noteEdit.setBounds (bar.removeFromRight (juce::jlimit (90, 260, flexible * 24 / 100))); bar.removeFromRight (6);
    snapshotBox.setBounds (bar);

    levels.setBounds (0, kBarHeight, getWidth(), kLevelHeight);
    auto content = getLocalBounds().withTrimmedTop (kBarHeight + kLevelHeight).reduced (kGap);
    auto strip = content.removeFromBottom (kStripHeight);
    content.removeFromBottom (kGap);
    layoutRack (content);

    // sloty na fotky pod rackem, stejne siroke jako rack, ve stejnem poradi; na konci "+" (pridat pristroj)
    strip = strip.withX (rackArea.getX()).withWidth (rackArea.getWidth());
    if (rackArea.getBottom() + kGap + kStripHeight <= getHeight() - kGap) strip.setY (rackArea.getBottom() + kGap);
    addTile.setBounds (strip.removeFromRight (juce::jmin (110, strip.getWidth() / 4)));
    strip.removeFromRight (kGap);
    const int n = order.size();
    for (int i = 0; i < n; ++i)
    {
        auto it = slots.find (order[i]);
        if (it == slots.end()) continue;
        const int x0 = strip.getX() + (strip.getWidth() + kGap) * i / juce::jmax (1, n);
        const int x1 = strip.getX() + (strip.getWidth() + kGap) * (i + 1) / juce::jmax (1, n) - kGap;
        it->second->setBounds (x0, strip.getY(), x1 - x0, strip.getHeight());
    }
    tip.setVisible (false);
    repaint();
}

//==============================================================================
// Rack uzivatele: presouvani (chytit panel mimo ovladace / lunchbox za sasi), pridavani, odebirani.
// Zmeny se hned ulozi do ~/Music/Rack Memory/rack.json a plati pro vsechny projekty.
namespace { const String kBoxId ("@lunchbox"); }

static void moveBox (rm::RackLayout& r, int index)
{
    // vsechny 500 moduly jako jeden blok na misto `index` (index v puvodnim poradi)
    std::vector<rm::RackItem> mods, others;
    int before = 0;
    for (int i = 0; i < (int) r.items.size(); ++i)
    {
        const auto& it = r.items[(size_t) i];
        if (r.width500 (it) > 0) mods.push_back (it);
        else { others.push_back (it); if (i < index) ++before; }
    }
    r.items = others;
    r.items.insert (r.items.begin() + juce::jlimit (0, (int) r.items.size(), before), mods.begin(), mods.end());
}

void RackMemoryEditor::handleGrab (const String& deviceId, const juce::MouseEvent& e, int phase)
{
    const auto pos = e.getEventRelativeTo (this).getPosition();
    juce::Rectangle<int> source;
    if (deviceId == kBoxId) { if (lunchboxes.empty()) return; source = lunchboxes[0].box.toNearestInt(); }
    else if (auto* pv = panelFor (deviceId)) source = pv->getBounds();
    else return;

    if (phase == 0)
    {
        grabbed = deviceId; grabStart = grabNow = pos; grabActive = false; dropIndex = dropSlot = -1;
        grabSource = source;
        return;
    }
    if (grabbed != deviceId) return;

    if (phase == 1)
    {
        grabNow = pos;
        if (! grabActive && grabStart.getDistanceFrom (pos) > 6)
        {
            grabActive = true;
            tip.setVisible (false);
            auto snapImg = createComponentSnapshot (source, true, 1.0f);
            const float k = juce::jmin (1.0f, 420.0f / (float) juce::jmax (1, snapImg.getWidth()), 300.0f / (float) juce::jmax (1, snapImg.getHeight()));
            grabImage = snapImg.rescaled (juce::jmax (1, juce::roundToInt (snapImg.getWidth() * k)), juce::jmax (1, juce::roundToInt (snapImg.getHeight() * k)));
            grabOffset = { juce::roundToInt ((grabStart.x - source.getX()) * k), juce::roundToInt ((grabStart.y - source.getY()) * k) };
        }
        if (grabActive) { computeDrop (pos); repaint(); }
        return;
    }

    // konec tahu
    if (grabActive && dropValid && ! dropMarker.isEmpty())
    {
        auto r = proc.rack;
        if (deviceId == kBoxId)                       moveBox (r, dropIndex);
        else if (dropSlot >= 0)                       r.place500 (deviceId, dropSlot);
        else if (dropIndex >= 0)                      r.move (deviceId, dropIndex);
        auto res = proc.setRack (r);
        if (res.failed()) showError (res.getErrorMessage());
        librarySignature = proc.library.signature();
        syncComponents();
    }
    grabActive = false; grabbed.clear(); grabImage = {}; dropIndex = dropSlot = -1; dropMarker = {}; dropValid = true;
    resized();
}

int RackMemoryEditor::slotAt (float x) const
{
    if (lunchboxes.empty()) return -1;
    const auto& lb = lunchboxes[0];
    return (int) std::floor ((x - lb.inner().getX()) / juce::jmax (1.0f, lb.slotW));
}

void RackMemoryEditor::computeDrop (juce::Point<int> p)
{
    dropIndex = dropSlot = -1; dropValid = true; dropMarker = {};
    const auto& rack = proc.rack;
    auto* item = rack.find (grabbed);

    // 500 modul: jen do lunchboxu, na slot (prichyceni k mrizce)
    if (item != nullptr && rack.width500 (*item) > 0)
    {
        if (lunchboxes.empty()) return;
        const auto& lb = lunchboxes[0];
        if (! lb.box.expanded (lb.slotW, lb.box.getHeight() * 0.25f).contains (p.toFloat())) return;
        const int w = rack.width500 (*item);
        const float left = (float) (p.x - (grabStart.x - grabSource.getX()));
        const int slot = juce::jlimit (0, juce::jmax (0, rack.lunchboxSlots - w), juce::roundToInt ((left - lb.inner().getX()) / juce::jmax (1.0f, lb.slotW)));
        if (slot == item->slot) return;                                // stejne misto
        auto trial = rack;
        dropValid = trial.place500 (grabbed, slot);
        dropSlot = slot;
        dropMarker = juce::Rectangle<float> (lb.inner().getX() + lb.slotW * (float) slot, lb.inner().getY(), lb.slotW * (float) w, lb.inner().getHeight()).toNearestInt();
        return;
    }

    // 19" pristroj nebo cely lunchbox: mezi radky
    auto firstMod = [&rack] { for (int i = 0; i < (int) rack.items.size(); ++i) if (rack.width500 (rack.items[(size_t) i]) > 0) return i; return -1; };
    auto lastMod  = [&rack] { for (int i = (int) rack.items.size() - 1; i >= 0; --i) if (rack.width500 (rack.items[(size_t) i]) > 0) return i; return -1; };
    auto indexOf  = [&rack] (const String& id) { for (int i = 0; i < (int) rack.items.size(); ++i) if (rack.items[(size_t) i].id == id) return i; return -1; };
    float best = 1e9f;
    for (auto& row : rows)
    {
        const auto& b = row.second;
        const int before = row.first.isEmpty() ? firstMod() : indexOf (row.first);
        const int after  = row.first.isEmpty() ? lastMod() + 1 : indexOf (row.first) + 1;
        for (auto cand : { std::make_pair (before, juce::Line<float> (b.getX(), b.getY(), b.getRight(), b.getY())),
                           std::make_pair (after,  juce::Line<float> (b.getX(), b.getBottom(), b.getRight(), b.getBottom())) })
        {
            juce::Point<float> nearest;
            const float d = cand.second.getDistanceFromPoint (p.toFloat(), nearest);
            if (d < best)
            {
                best = d; dropIndex = cand.first;
                dropMarker = juce::Rectangle<float> (cand.second.getStartX(), cand.second.getStartY() - 2.5f, cand.second.getLength(), 5.0f).toNearestInt();
            }
        }
    }
    // pusteni na stejne misto = nic
    auto trial = rack;
    if (grabbed == kBoxId) moveBox (trial, dropIndex); else trial.move (grabbed, dropIndex);
    if (trial == rack) dropMarker = {};
}

void RackMemoryEditor::mouseDown (const juce::MouseEvent& e)
{
    if (lunchboxes.empty() || ! lunchboxes[0].box.contains (e.position)) return;
    if (e.mods.isPopupMenu()) { showLunchboxMenu(); return; }
    handleGrab (kBoxId, e, 0);
}
void RackMemoryEditor::mouseDrag (const juce::MouseEvent& e)   { if (grabbed == kBoxId) handleGrab (kBoxId, e, 1); }
void RackMemoryEditor::mouseUp (const juce::MouseEvent& e)     { if (grabbed == kBoxId) handleGrab (kBoxId, e, 2); }

void RackMemoryEditor::showAddMenu()
{
    juce::PopupMenu m, m19, m500;
    std::vector<String> ids;
    for (auto& p : rm::profiles())
    {
        if (p.id == "elysia.xfilter.500") continue;          // ve skutecnosti ME (viz profily)
        ids.push_back (p.id);
        (is500 (&p) ? m500 : m19).addItem ((int) ids.size(), p.name);
    }
    m.addSectionHeader (U ("Přidat přístroj do racku"));
    m.addSubMenu (U ("19\" rack"), m19);
    m.addSubMenu (U ("500 modul (lunchbox)"), m500);
    const auto missing = proc.missingInstances();
    if (! missing.isEmpty())
    {
        m.addSeparator();
        m.addItem (1000, U ("Přidat přístroje z této verze (") + String (missing.size()) + ")");
    }
    juce::Component::SafePointer<RackMemoryEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTile), [safe, ids, missing] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto rack = safe->proc.rack;
        if (r == 1000)
        {
            for (auto& id : missing)
                if (rack.find (id) == nullptr)
                {
                    rm::RackItem it; it.id = id; it.model = rm::profileIdOf (id);
                    rack.items.push_back (it);     // slot doplni normalise()
                }
        }
        else if (r >= 1 && r <= (int) ids.size()) rack.add (ids[(size_t) r - 1]);
        auto res = safe->proc.setRack (rack);
        if (res.failed()) safe->showError (res.getErrorMessage());
        safe->librarySignature = safe->proc.library.signature();
        safe->refreshAll();
    });
}

void RackMemoryEditor::removeDevice (const String& id)
{
    const auto name = proc.rack.displayName (id);
    juce::Component::SafePointer<RackMemoryEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle (U ("Odebrat z racku"))
                                      .withMessage (U ("Odebrat ") + name + U (" z racku?\nUložená nastavení ve verzích zůstanou a přístroj jde kdykoli přidat zpět."))
                                      .withButton (U ("Odebrat")).withButton (U ("Zrušit")).withAssociatedComponent (this),
                                  [safe, id] (int result)
                                  {
                                      if (safe == nullptr || result != 1) return;
                                      auto rack = safe->proc.rack;
                                      rack.remove (id);
                                      auto res = safe->proc.setRack (rack);
                                      if (res.failed()) safe->showError (res.getErrorMessage());
                                      safe->proc.devicePhotos.erase (id);
                                      safe->librarySignature = safe->proc.library.signature();
                                      safe->refreshAll();
                                  });
}

void RackMemoryEditor::showLunchboxMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (U ("Lunchbox"));
    int used = 0;
    for (auto& it : proc.rack.items) if (proc.rack.width500 (it) > 0) used = juce::jmax (used, it.slot + proc.rack.width500 (it));
    for (int n : { 6, 8, 10, 11, 16 })
        m.addItem (n, String (n) + U (" slotů"), n >= used, n == proc.rack.lunchboxSlots);
    juce::Component::SafePointer<RackMemoryEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto rack = safe->proc.rack;
        rack.lunchboxSlots = r;
        safe->proc.setRack (rack);
        safe->librarySignature = safe->proc.library.signature();
        safe->refreshAll();
    });
}

void RackMemoryEditor::entryLeave (PanelView* from, bool forward)
{
    for (int i = 0; i < order.size(); ++i)
        if (panelFor (order[i]) == from)
        {
            const int j = i + (forward ? 1 : -1);
            if (j >= 0 && j < order.size())
                if (auto* pv = panelFor (order[j])) pv->startEntryEdge (forward);
            return;
        }
}

//==============================================================================
void RackMemoryEditor::timerCallback()
{
    if (savedFlash > 0 && --savedFlash == 0)
        refreshTopBar();

    ++tick;
    if (tick % 20 == 0) checkForUpdate();        // novy balicek ve Stazenych? (kazdych 10 s)
    if (updater->getRevision() != updaterRevision) { updaterRevision = updater->getRevision(); refreshUpdateButton(); }

    // zmeny z jine instance pluginu (jina stopa / jiny projekt) - kontrola jednou za 2 s
    if ((tick % 4) != 0) return;
    auto sig = proc.library.signature();
    if (sig != librarySignature)
    {
        librarySignature = sig;
        const auto before = proc.rack;
        proc.reloadRack();                     // rack zmeneny v jine instanci / jinem projektu
        if (proc.rack != before && ! grabActive) refreshAll();
        proc.reloadLibrary();
        proc.commitState();
        rebuildSnapshotBox();
    }
}

void RackMemoryEditor::rebuildSnapshotBox()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    snapshotBox.clear (juce::dontSendNotification);

    auto itemText = [] (const rm::Snapshot& s)
    {
        String t = s.title() + "     " + rm::formatTime (s.modified);
        if (s.notes.isNotEmpty()) t << "     " << s.notes.upToFirstOccurrenceOf ("\n", false, false).substring (0, 40);
        return t;
    };

    // verze teto skladby nahore, ostatni skladby pod nimi
    for (int i = 0; i < (int) proc.all.size(); ++i)
        if (proc.all[(size_t) i].project == proc.songName)
            snapshotBox.addItem (itemText (proc.all[(size_t) i]), i + 1);

    juce::StringArray songs;
    for (auto& s : proc.all)
        if (s.project != proc.songName) songs.addIfNotAlreadyThere (s.project);
    for (auto& song : songs)
    {
        snapshotBox.addSectionHeading (song.isEmpty() ? U ("Bez názvu") : song);
        for (int i = 0; i < (int) proc.all.size(); ++i)
            if (proc.all[(size_t) i].project == song)
                snapshotBox.addItem (itemText (proc.all[(size_t) i]), i + 1);
    }

    int selected = 0;
    for (int i = 0; i < (int) proc.all.size(); ++i)
        if (proc.all[(size_t) i].id == proc.loadedId) selected = i + 1;
    snapshotBox.setSelectedId (selected, juce::dontSendNotification);
}

void RackMemoryEditor::refreshTopBar()
{
    const bool needsSave = proc.dirty || proc.loadedId.isEmpty();
    saveButton.setButtonText (savedFlash > 0 ? U ("ULOŽENO ") + proc.working.version : U ("ULOŽIT"));
    saveButton.setColour (juce::TextButton::buttonColourId, savedFlash > 0 ? kOk
                                                           : needsSave && proc.working.numKnown() > 0 ? kAccent : Colour (0xff2a2e35));
    saveButton.setColour (juce::TextButton::textColourOffId, (savedFlash > 0 || (needsSave && proc.working.numKnown() > 0)) ? Colour (0xff111111) : kText);

    if (undoButton.isVisible() != proc.canUndo())
    {
        undoButton.setVisible (proc.canUndo());
        resized();
    }
}

void RackMemoryEditor::refreshAll()
{
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        if (songEdit.getText().trim() != proc.songName) songEdit.setText (proc.songName, false);
        if (noteEdit.getText() != proc.working.notes)   noteEdit.setText (proc.working.notes, false);
        valuesButton.setToggleState (proc.showValues, juce::dontSendNotification);
    }
    syncComponents();
    rebuildSnapshotBox();
    undoButton.setVisible (proc.canUndo());
    resized();
    for (auto& kv : panels) { kv.second->setShowValues (proc.showValues); kv.second->repaint(); }
    refreshPhotos();
    levels.refresh();
    refreshTopBar();
}

void RackMemoryEditor::edited()
{
    proc.markDirty();
    refreshTopBar();
}

void RackMemoryEditor::showTip (PanelView& panel, const String& text, juce::Point<int> where, int radius)
{
    if (text.isEmpty() || grabActive || panel.isEditing()) { tip.setVisible (false); return; }
    tip.setText (text, juce::dontSendNotification);
    const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (tip.getFont(), text)) + 22, h = 24;
    const auto p = getLocalPoint (&panel, where);
    const int x = juce::jlimit (4, juce::jmax (4, getWidth() - w - 4), p.x - w / 2);
    int y = p.y - radius - h - 8;                              // nad ovladacem
    if (y < kBarHeight + kLevelHeight + 2) y = p.y + radius + 8;   // u horniho okraje pod nim
    tip.setBounds (x, y, w, h);
    tip.setVisible (true);
    tip.toFront (false);
}

//==============================================================================
void RackMemoryEditor::doSave()
{
    proc.songName = songEdit.getText().trim();
    proc.working.notes = noteEdit.getText();
    auto r = proc.save();
    if (r.failed()) { showError (r.getErrorMessage()); return; }
    librarySignature = proc.library.signature();
    savedFlash = 4;     // ~2 s
    refreshAll();
}

void RackMemoryEditor::showMenu()
{
    juce::PopupMenu m;
    const int nPhotos = proc.working.photos.size();
    m.addItem (2, U ("Otevřít fotky (") + String (nPhotos) + ")", nPhotos > 0);
    m.addSeparator();
    m.addItem (3, U ("Textový report"));
    m.addItem (4, U ("Složka s daty"));
    m.addSeparator();
    m.addItem (5, U ("Vymazat rack (vše nezadáno)"));
    m.addItem (6, U ("Smazat načtenou verzi"), proc.loadedId.isNotEmpty());
    m.addSeparator();
    m.addItem (7, U ("Vrátit výchozí rack"));
    m.addItem (10, U ("Zkontrolovat aktualizace"));
    m.addItem (8, U ("Aktualizovat ze souboru…"));
    m.addItem (9, U ("Verze ") + String (JucePlugin_VersionString), false);

    juce::Component::SafePointer<RackMemoryEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& p = safe->proc;
        if (r == 2)
        {
            for (auto& name : p.working.photos)
            {
                auto f = p.library.photoFile (name);
                if (f.existsAsFile()) f.startAsProcess();
            }
        }
        else if (r == 3)
        {
            auto dir = p.library.reportsDir(); dir.createDirectory();
            auto file = dir.getNonexistentChildFile (juce::File::createLegalFileName (p.working.title().replace ("  ", "_")), ".txt", false);
            if (file.replaceWithText (rm::makeReport (p.working), false, false, "\n")) file.startAsProcess();
        }
        else if (r == 4) { p.library.getRoot().createDirectory(); p.library.getRoot().revealToUser(); }
        else if (r == 5) { p.clearAll(); safe->refreshAll(); }
        else if (r == 6)
        {
            auto res = p.deleteSnapshot (p.loadedId);    // jde do slozky Trash, da se obnovit
            if (res.failed()) safe->showError (res.getErrorMessage());
            safe->librarySignature = p.library.signature();
            safe->refreshAll();
        }
        else if (r == 10) { safe->manualCheck = true; safe->updater->checkNow(); }
        else if (r == 7) { p.setRack (rm::RackLayout::makeDefault()); safe->librarySignature = p.library.signature(); safe->refreshAll(); }
        else if (r == 8)
        {
            safe->chooser = std::make_unique<juce::FileChooser> (U ("Nová verze Rack Memory (.zip)"),
                                                                 juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"), "*.zip");
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [safe] (const juce::FileChooser& fc)
                                        {
                                            auto f = fc.getResult();
                                            if (safe != nullptr && f.existsAsFile()) safe->runUpdate (f);
                                        });
        }
    });
}

void RackMemoryEditor::showError (const String& m)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Rack Memory").withMessage (m).withButton ("OK").withAssociatedComponent (this),
                                  nullptr);
}

void RackMemoryEditor::showInfo (const String& m)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle ("Rack Memory").withMessage (m).withButton ("OK").withAssociatedComponent (this),
                                  nullptr);
}

//==============================================================================
// Aktualizace: kamos dostane "Rack Memory 0.5.1.zip" (do Stazenych) -> v pluginu se objevi tlacitko.
RackMemoryEditor::Update RackMemoryEditor::findUpdate (const juce::Array<juce::File>& folders, const String& current)
{
    Update best;
    juce::int64 bestV = versionValue (current);
    for (auto& dir : folders)
    {
        if (! dir.isDirectory()) continue;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "Rack Memory*.zip"))
        {
            const auto v = f.getFileNameWithoutExtension().fromFirstOccurrenceOf ("Rack Memory", false, true).trim();
            if (v.isEmpty() || ! v.containsOnly ("0123456789.")) continue;
            const auto vv = versionValue (v);
            if (vv > bestV) { bestV = vv; best.file = f; best.version = v; }
        }
    }
    return best;
}

juce::Result RackMemoryEditor::installUpdate (const juce::File& package, const juce::File& componentsDir)
{
    return rm::installComponentFromZip (package, componentsDir);
}

// Tlacitko nahore: 1) nova verze na GitHubu (stahne, overi, nainstaluje), 2) novejsi zip ve Stazenych / na Plose.
void RackMemoryEditor::checkForUpdate()
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    auto u = findUpdate ({ home.getChildFile ("Downloads"), home.getChildFile ("Desktop") }, JucePlugin_VersionString);
    if (u.file != update.file) { update = u; refreshUpdateButton(); }
}

void RackMemoryEditor::refreshUpdateButton()
{
    using S = rm::Updater::State;
    const auto st = updater->getStatus();
    String text; bool visible = false, enabled = true;
    switch (st.state)
    {
        case S::available:   text = U ("AKTUALIZOVAT ") + st.latest.version; visible = true; break;
        case S::downloading: text = U ("stahuji ") + String (juce::roundToInt (st.progress * 100.0f)) + " %"; visible = true; enabled = false; break;
        case S::installing:  text = U ("instaluji…"); visible = true; enabled = false; break;
        case S::installed:   text = U ("RESTARTUJ LOGIC"); visible = true; enabled = false; break;
        case S::failed:      if (updateRequested) { text = U ("ZKUSIT ZNOVU"); visible = true; } break;
        default: break;
    }
    if (! visible && update.file.existsAsFile()) { text = U ("AKTUALIZOVAT ") + update.version; visible = true; }
    const bool changed = updateButton.isVisible() != visible;
    updateButton.setButtonText (text);
    updateButton.setEnabled (enabled);
    updateButton.setVisible (visible);
    if (changed) resized();

    // vysledek akce, o kterou si uzivatel rekl
    if (updateRequested && st.state == S::installed)
    {
        updateRequested = false;
        showInfo (U ("Rack Memory ") + st.latest.version + U (" je nainstalovaný.\nZavři a znovu otevři Logic.\n\nTvoje nastavení, verze skladeb i rack zůstaly beze změny."));
    }
    else if (updateRequested && st.state == S::failed && lastUpdateError != st.message)
    {
        lastUpdateError = st.message;
        showError (U ("Aktualizace se nepovedla: ") + st.message + U ("\nNainstalovaná verze zůstala beze změny."));
    }
    else if (manualCheck && (st.state == S::upToDate || st.state == S::offline || st.state == S::notConfigured
                             || (st.state == S::failed && ! updateRequested)))
    {
        manualCheck = false;
        showInfo (st.state == S::notConfigured ? U ("Adresa pro aktualizace není nastavená.") : st.message);
    }
}

void RackMemoryEditor::startUpdate()
{
    using S = rm::Updater::State;
    const auto st = updater->getStatus().state;
    if (st == S::available) { updateRequested = true; lastUpdateError.clear(); updater->downloadAndInstall(); }
    else if (st == S::failed && updateRequested) { updater->checkNow(); }
    else if (update.file.existsAsFile()) runUpdate (update.file);
    refreshUpdateButton();
}

void RackMemoryEditor::runUpdate (const juce::File& package)
{
    updateButton.setEnabled (false);
    updateButton.setButtonText (U ("instaluji…"));
    juce::Component::SafePointer<RackMemoryEditor> safe (this);
    juce::Thread::launch ([safe, package]
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/Components");
        const auto r = installUpdate (package, dir);
        juce::MessageManager::callAsync ([safe, r]
        {
            if (safe == nullptr) return;
            safe->updateButton.setEnabled (true);
            if (r.wasOk())
            {
                safe->update = {};
                safe->updateButton.setVisible (false);
                safe->resized();
                safe->showInfo (U ("Nová verze je nainstalovaná.\nZavři a znovu otevři Logic."));
            }
            else
            {
                safe->updateButton.setButtonText (U ("AKTUALIZOVAT ") + safe->update.version);
                safe->showError (r.getErrorMessage());
            }
        });
    });
}

//==============================================================================
void RackMemoryEditor::refreshPhotos()
{
    bool any = false;
    for (auto& id : order)
    {
        auto it = proc.devicePhotos.find (id);
        const auto* st = it != proc.devicePhotos.end() ? &it->second : nullptr;
        if (auto s = slots.find (id); s != slots.end()) { s->second->state = st; s->second->repaint(); }
        if (auto* pv = panelFor (id)) pv->setPhoto ({}, false);     // vzhled hardwaru se podle fotky nemeni
        any = any || (st != nullptr && st->rectified.isValid());
    }
    juce::ignoreUnused (any);
    const auto missing = proc.missingInstances();
    addTile.addHint = missing.isEmpty() ? String() : String (missing.size()) + U (" z verze chybí");
    addTile.repaint();
}

bool RackMemoryEditor::isInterestedInFileDrag (const juce::StringArray& files)   { return anyPhoto (files); }

void RackMemoryEditor::filesDropped (const juce::StringArray& files, int, int)
{
    // mimo konkretni pristroj: plugin sam zjisti, co na fotce je (i vic pristroju najednou)
    for (auto& f : files)
        if (juce::File (f).hasFileExtension (kPhotoTypes)) importPhotoAuto (juce::File (f));
}

void RackMemoryEditor::choosePhoto (const String& deviceId)
{
    chooser = std::make_unique<juce::FileChooser> (U ("Fotka přístroje"),
                                                   juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
                                                   "*.jpg;*.jpeg;*.png;*.gif;*.heic;*.heif;*.tif;*.tiff");
    juce::Component::SafePointer<RackMemoryEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, deviceId] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (safe != nullptr && f.existsAsFile()) safe->importPhoto (f, deviceId);
                          });
}

void RackMemoryEditor::importPhoto (const juce::File& source, const String& deviceId)
{
    auto* prof = rm::profileForInstance (deviceId);
    if (prof == nullptr) return;

    auto& st = proc.devicePhotos[deviceId];
    st.busy = true; st.failed = false; st.status.clear();
    refreshPhotos();

    const auto lib = proc.library;
    const auto snapId = proc.working.id;
    const auto reference = loadAsset (prof->image).createCopy();
    juce::Component::SafePointer<RackMemoryEditor> safe (this);

    // Vse na pozadi (mimo audio i UI vlakno) a jen na tomto pocitaci - fotka se nikam neodesila.
    juce::Thread::launch ([safe, lib, snapId, source, prof, deviceId, reference]
    {
        String copyError, loadError;
        const auto name = lib.importPhoto (source, snapId, copyError);          // original se uchova v knihovne
        rmcv::Result result;
        auto img = rmcv::loadPhoto (source, loadError);
        if (img.isValid()) result = rmcv::recognize (img, *prof, reference);
        else               result.message = loadError;
        const auto preview = img.isValid() ? img.rescaled (juce::jmax (1, img.getWidth() * 1600 / juce::jmax (img.getWidth(), img.getHeight())),
                                                           juce::jmax (1, img.getHeight() * 1600 / juce::jmax (img.getWidth(), img.getHeight()))) : juce::Image();

        juce::MessageManager::callAsync ([safe, name, result, deviceId, preview]
        {
            if (safe == nullptr) return;
            auto& p = safe->proc;
            if (name.isNotEmpty()) p.working.photos.addIfNotAlreadyThere (name);

            auto& ds = p.devicePhotos[deviceId];
            ds.busy = false;
            ds.read = result.read; ds.readable = result.readable;
            if (result.aligned)
            {
                ds.failed = false;
                ds.rectified = result.rectified;
                ds.original = preview;
                ds.status = result.message;
                p.applyRecognition (deviceId, result.values);
            }
            else
            {
                ds.failed = true;
                ds.rectified = {};
                ds.status = result.message;
                p.markDirty();
            }
            safe->refreshAll();
        });
    });
}

void RackMemoryEditor::importPhotoAuto (const juce::File& source)
{
    struct Job { String id; const rm::DeviceProfile* prof; juce::Image ref; };
    auto jobs = std::make_shared<std::vector<Job>>();
    juce::StringArray models;
    for (auto& id : order)
        if (auto* prof = rm::profileForInstance (id))
        {
            if (models.contains (prof->id)) continue;       // dalsi kus stejneho modelu: fotku pust primo na nej
            models.add (prof->id);
            jobs->push_back ({ id, prof, loadAsset (prof->image).createCopy() });
            auto& st = proc.devicePhotos[id];
            st.busy = true;
        }
    refreshPhotos();

    const auto lib = proc.library;
    const auto snapId = proc.working.id;
    juce::Component::SafePointer<RackMemoryEditor> safe (this);

    juce::Thread::launch ([safe, lib, snapId, source, jobs]
    {
        String copyError, loadError;
        const auto name = lib.importPhoto (source, snapId, copyError);
        auto img = rmcv::loadPhoto (source, loadError);
        const auto preview = img.isValid() ? img.rescaled (juce::jmax (1, img.getWidth() * 1600 / juce::jmax (img.getWidth(), img.getHeight())),
                                                           juce::jmax (1, img.getHeight() * 1600 / juce::jmax (img.getWidth(), img.getHeight()))) : juce::Image();
        auto results = std::make_shared<std::vector<rmcv::Result>> (jobs->size());
        if (img.isValid())
        {
            // pristroje paralelne (kazdy ma vlastni vypocet), fotka je sdilena jen pro cteni
            std::vector<std::thread> threads;
            for (size_t i = 0; i < jobs->size(); ++i)
                threads.emplace_back ([&, i] { (*results)[i] = rmcv::recognize (img, *(*jobs)[i].prof, (*jobs)[i].ref); });
            for (auto& t : threads) t.join();
        }

        juce::MessageManager::callAsync ([safe, name, results, jobs, loadError, preview]
        {
            if (safe == nullptr) return;
            auto& p = safe->proc;
            if (name.isNotEmpty()) p.working.photos.addIfNotAlreadyThere (name);

            // co na fotce opravdu je: nalezene pristroje; kdyz jich je vic, jen ty s podobne silnou shodou
            int bestKp = 0;
            for (auto& r : *results) if (r.aligned) bestKp = juce::jmax (bestKp, r.keypoints);
            std::map<String, std::map<String, rm::ControlValue>> apply;
            for (size_t i = 0; i < jobs->size(); ++i)
            {
                auto& r = (*results)[i];
                auto& ds = p.devicePhotos[(*jobs)[i].id];
                ds.busy = false;
                const bool take = r.aligned && (r.keypoints >= bestKp / 2 || bestKp < 12);
                if (! take) continue;
                ds.failed = false; ds.rectified = r.rectified; ds.original = preview; ds.status = r.message;
                ds.read = r.read; ds.readable = r.readable;
                apply[(*jobs)[i].id] = r.values;
            }
            if (! apply.empty()) p.applyRecognition (apply);
            safe->refreshAll();
            if (apply.empty())
                safe->showError (loadError.isNotEmpty() ? loadError
                                                        : U ("Na fotce jsem nepoznal žádný přístroj z racku.\nFoť zepředu, celý přístroj, ostře – nebo fotku pusť přímo na přístroj."));
        });
    });
}
