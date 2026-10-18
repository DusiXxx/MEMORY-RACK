#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "Updater.h"
#include <array>

inline juce::String U (const char* utf8)   { return juce::String::fromUTF8 (utf8); }

//==============================================================================
// Policko pro napsani hodnoty (Tab / Shift+Tab = dalsi / predchozi ovladac)
struct EntryBox : public juce::TextEditor
{
    std::function<void (bool forward)> onTab;
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::tabKey) { if (onTab) onTab (! k.getModifiers().isShiftDown()); return true; }
        return juce::TextEditor::keyPressed (k);
    }
};

//==============================================================================
// Jeden pristroj: obrazek panelu + ovladace, ktere se daji nastavit mysi nebo napsat.
class PanelView : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    ~PanelView() override;
    void setData (const rm::DeviceProfile* profile, rm::Snapshot* snapshot, const juce::String& instanceId);
    const juce::String& getInstanceId() const    { return instanceId; }
    void setShowValues (bool);
    void setPhoto (const juce::Image& rectified, bool show);     // fotka uzivatele narovnana na panel
    const rm::DeviceProfile* getProfile() const   { return profile; }
    void setFocus (juce::Rectangle<float> profileArea)   { focus = profileArea; updateTransform(); repaint(); }   // detail: jen cast panelu
    static bool highContrast;                    // vyrazne ukazatele (cerny obrys + zluta rucicka)
    static bool overlayPointers;
    static float pointerWidth (float knobRadius)   { return highContrast ? juce::jmax (3.2f, knobRadius * 0.15f) : juce::jmax (2.2f, knobRadius * 0.095f); }                 // ukazatel pres fotku knobu (testy: vypnuto = jako skutecny pristroj)
    juce::Rectangle<int> controlBounds (int index) const;
    int indexOf (const juce::String& controlId) const;

    std::function<void (const juce::File&)> onPhotoDropped;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    std::function<void()> onEdit;                                              // hodnota zmenena
    std::function<void (const juce::String&, juce::Point<int>, int)> onHover;  // text, stred ovladace, polomer
    std::function<void (const juce::MouseEvent&, int phase)> onGrab;           // presun pristroje: 0 start, 1 tah, 2 konec
    std::function<void (PanelView*, bool forward)> onEntryLeave;               // Tab za posledni / pred prvni ovladac
    std::function<void (int controlIndex)> onDetail;                          // kliknuti na knob (bez tahu) = detail
    std::function<void()> onDeviceMenu;                                        // prave tlacitko mimo ovladace

    // psani hodnot
    void startEntry (int index);
    void startEntryEdge (bool first);
    bool isEditing() const                        { return entry != nullptr; }
    std::vector<int> readingOrder() const;       // poradi ovladacu pri Tab (zleva doprava, shora dolu)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    const rm::DeviceProfile* profile = nullptr;
    rm::Snapshot* snap = nullptr;
    juce::String instanceId;         // kus v racku (klic hodnot)

    int hover = -1, dragging = -1;
    double dragStart = 0;
    bool dragStartKnown = false, dragMoved = false, grabbing = false;
    double wheelAccum = 0;

    float scale = 1.0f;
    juce::Rectangle<float> panel, focus;
    bool showValues = false, showPhoto = false, dragOver = false;
    juce::Image background, scaledBackground, photo;
    std::map<juce::String, juce::Image> sprites;

    std::unique_ptr<EntryBox> entry;
    int entryIndex = -1;
    bool entryClosing = false;

    rm::ControlValue value (const rm::ControlDef&) const;
    void updateTransform();
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> centre (const rm::ControlDef&) const;
    float radius (const rm::ControlDef&) const;
    int hitTest (juce::Point<float>) const;
    void setValue (int index, bool known, double pos);
    void setValueFull (int index, const rm::ControlValue&);
    void showMenu (int index);
    void drawControl (juce::Graphics&, int index);
    void drawPointer (juce::Graphics&, juce::Point<float> c, float r, float angleDeg, bool own);
    void drawVector (juce::Graphics&, const rm::ControlDef&, const rm::ControlValue&, juce::Point<float> p, float r);
    int buttonAt (const rm::ControlDef&, juce::Point<float>) const;      // vbuttons: ktere tlacitko (-1 = zadne)
    void sendHover (int index);
    juce::String tipFor (const rm::ControlDef&) const;
    juce::Rectangle<int> entryBounds (int index) const;
    bool commitEntry();                          // false = text se neda pouzit (policko zustane)
    void closeEntry();
    void moveEntry (bool forward);
};

//==============================================================================
// Misto pro fotku jednoho pristroje: pretahnout sem, nebo kliknout a vybrat soubor.
class PhotoSlot : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    const rm::DeviceProfile* profile = nullptr;
    const RackMemoryProcessor::DevicePhoto* state = nullptr;
    juce::String title;                      // nazev kusu v racku
    bool isAdd = false;                      // dlazdice "+ pridat pristroj"
    juce::String addHint;                    // napr. "2 chybí"
    std::function<void (const juce::File&)> onPhoto;
    std::function<void()> onClick, onRemove, onView, onMenu;
    int toReview = 0;                        // hodnoty z fotky cekajici na kontrolu
    bool hasSavedPhoto = false;              // fotka je v knihovne (da se znovu otevrit)

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override   { const bool o = removeArea().contains (e.position); if (o != overRemove) { overRemove = o; repaint(); } }
    void mouseEnter (const juce::MouseEvent&) override    { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override     { hover = false; overRemove = false; repaint(); }
    juce::Rectangle<float> removeArea() const             { return { (float) getWidth() - 24.0f, 4.0f, 20.0f, 20.0f }; }
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    bool hover = false, dragOver = false, overRemove = false;
    juce::Rectangle<float> thumb;            // nahled fotky (klik = zvetsit)
};

//==============================================================================
// Velka fotka pres rack. Vzhled panelu se podle fotky nemeni. Umi rucni zarovnani: 4 kliknuti na rohy panelu.
class PhotoViewer : public juce::Component
{
public:
    PhotoViewer();
    juce::Image image;
    juce::String caption;
    std::vector<juce::Point<float>> quad;            // kde plugin pristroj na fotce nasel (px obrazku)
    void startAlign (const juce::String& deviceName);
    std::function<void (std::array<juce::Point<float>, 4>)> onCorners;   // px obrazku: VL, VP, PD, LD
    std::function<void()> onAlignRequest;
    bool isAligning() const                           { return aligning; }
    juce::Rectangle<float> imageRect() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void close();
    juce::TextButton alignButton, closeButton;
private:
    bool aligning = false;
    juce::String alignName;
    std::vector<juce::Point<float>> clicks;
};

//==============================================================================
// Zalozky: PATCHBAY (nahore vlevo) + skupiny (Mixing, Mastering, vlastni) + "+". Vpravo stavova zprava.
class GroupTabs : public juce::Component
{
public:
    struct Tab { juce::String id, name; };
    std::vector<Tab> tabs;
    juce::String selected, status;
    juce::Colour statusColour { 0xff8b929e };
    std::function<void (const juce::String&)> onSelect, onMenu;
    std::function<void()> onAdd;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override   { hover = -2; repaint(); }
    juce::Rectangle<int> rectFor (const juce::String& id) const;
private:
    std::vector<juce::Rectangle<int>> rects;
    juce::Rectangle<int> addRect;
    int hover = -2;
};

//==============================================================================
// Obnova neulozenych zmen (projekt zavren bez ulozeni)
class RecoveryBanner : public juce::Component
{
public:
    RecoveryBanner();
    juce::String text;
    std::function<void()> onRestore, onDiscard;
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::TextButton restore, discard;
};

//==============================================================================
// Patchbay: dokumentace zapojeni (kabely vpredu, popisky bodu, rezimy normalizace). Nic se neprepina.
class PatchbayView : public juce::Component
{
public:
    explicit PatchbayView (RackMemoryProcessor&);
    void refresh();
    int naturalHeight (int width) const;
    std::function<void()> onChanged;
    std::function<void (const juce::String&)> onStatus;
    juce::Point<float> pointPosition (const juce::String& pointId) const;   // kvuli testum
    void clickPoint (const juce::String& pointId);                          // jako kliknuti mysi
    juce::String pointName (const juce::String& pointId) const;              // "S-patch Plus 5 · WA-2A OUT"
    const juce::String& pendingPoint() const                     { return pending; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override         { hoverPoint.clear(); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    RackMemoryProcessor& proc;
    struct Panel { juce::String id; const rm::DeviceProfile* prof = nullptr; juce::Rectangle<float> r; juce::Image img; };
    std::vector<Panel> panels;
    juce::Rectangle<int> addRect, listArea;
    std::vector<std::pair<juce::Rectangle<float>, int>> deleteAreas;
    juce::String pending, hoverPoint;
    juce::String pointAt (juce::Point<float>) const;
    juce::Rectangle<float> modeArea (const Panel&, int channel) const;
    const Panel* panelOf (const juce::String& pointId) const;
    juce::String labelOf (const juce::String& pointId) const;
    void editLabel (const juce::String& pointId);
    void pointMenu (const juce::String& pointId);
    void panelMenu (const Panel&);
    void setLabel (const juce::String& pointId, const juce::String& text);
    void setMode (const juce::String& pbId, int channel, const juce::String& mode);
    void showAddMenu();
};

//==============================================================================
// Zvetseny detail jednoho ovladace: potisk, hodnota, jednotka, stupnice, stav (rucne / z fotky / nezadano)
class ControlDetail : public juce::Component
{
public:
    ControlDetail();
    void show (RackMemoryProcessor&, const juce::String& instanceId, int controlIndex, const juce::Image& rectifiedPhoto);
    void refresh();
    std::function<void()> onEdit;
    juce::String getInstance() const       { return instance; }
    int getIndex() const                   { return index; }
    juce::String describe() const;         // text detailu (kvuli testum)
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    PanelView zoom, photoZoom;
    juce::TextButton confirmButton, unknownButton, typeButton, minusButton, plusButton, closeButton;
private:
    RackMemoryProcessor* proc = nullptr;
    juce::String instance;
    int index = -1;
    juce::Rectangle<int> card, textArea;
    void step (int dir);
};

//==============================================================================
// Kontrola po fotce: co se precetlo, co je nejiste (klik = detail ovladace), potvrzeni
class ReviewPanel : public juce::Component
{
public:
    ReviewPanel();
    struct Item { juce::String instance, title, message; int read = 0, readable = 0, unconfirmed = 0; juce::StringArray uncertain; bool failed = false; };
    void setItems (const std::vector<Item>&);
    const std::vector<Item>& getItems() const     { return items; }
    std::function<void (const juce::String& instance, const juce::String& controlId)> onOpen;
    std::function<void()> onConfirmAll, onClose;
    int preferredHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::TextButton confirmAll, closeButton;
private:
    std::vector<Item> items;
    struct Link { juce::Rectangle<float> r; juce::String instance, control; };
    std::vector<Link> links;
};

//==============================================================================
// Hlasitost a mereni: jedna instance = uroven + gain; dve instance (IN pred HW, OUT za HW) = obe urovne,
// rozdil, ktery dela hardware, a srovnani hlasitosti.
class LevelStrip : public juce::Component, private juce::Timer
{
public:
    explicit LevelStrip (RackMemoryProcessor&);
    ~LevelStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    juce::String deltaText() const;              // kvuli testum

private:
    RackMemoryProcessor& proc;
    juce::ComboBox roleBox, groupBox;
    juce::Slider ownGain, otherGain;
    std::unique_ptr<juce::SliderParameterAttachment> ownAttach;
    juce::TextButton matchButton, resetButton;
    juce::Rectangle<int> meterA, meterB, deltaArea;
    void timerCallback() override;
    void drawMeter (juce::Graphics&, juce::Rectangle<int>, const juce::String& label, const RackMemoryProcessor*, bool present);
};

//==============================================================================
class RackMemoryEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit RackMemoryEditor (RackMemoryProcessor&);
    ~RackMemoryEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void refreshAll();   // verejne kvuli testum
    PanelView* panelFor (const juce::String& deviceId) const;
    void importPhotoAuto (const juce::File&);    // fotka pustena kamkoli: pristroj(e) se poznaji sami
    void selectTab (const juce::String& id);     // "@patchbay" nebo ID skupiny
    void openDetail (const juce::String& instanceId, int controlIndex);
    void alignPhoto (const juce::String& deviceId, const std::array<juce::Point<float>, 4>& corners);   // rucni zarovnani
    int heightForWidth (int width) const;        // okno roste s rackem (stejny pomer)
    // kvuli testum
    GroupTabs tabs;
    RecoveryBanner banner;
    PatchbayView patchbay;
    ControlDetail detail;
    ReviewPanel review;
    juce::TextButton photoButton;
    juce::String statusText() const               { return tabs.status; }
    juce::Rectangle<int> getRackArea() const      { return rackArea; }

    // aktualizace z noveho balicku (Stazene / Plocha)
    struct Update { juce::File file; juce::String version; };
    static Update findUpdate (const juce::Array<juce::File>& folders, const juce::String& currentVersion);
    static juce::Result installUpdate (const juce::File& package, const juce::File& componentsDir);

private:
    RackMemoryProcessor& proc;

    juce::TextEditor songEdit, noteEdit;
    juce::ComboBox snapshotBox;
    juce::TextButton saveButton, undoButton, updateButton, valuesButton, menuButton;
    juce::Label tip;

    juce::StringArray order;                                     // aktualni poradi v racku
    std::map<juce::String, std::unique_ptr<PanelView>> panels;
    std::map<juce::String, std::unique_ptr<PhotoSlot>> slots;
    PhotoSlot addTile;
    PhotoViewer viewer;
    LevelStrip levels;
    juce::Rectangle<int> rackArea;

    struct Lunchbox { juce::Rectangle<float> box; int slots = 8; float slotW = 0, rail = 0, wall = 0; std::vector<int> used;
                      juce::Rectangle<float> inner() const { return box.reduced (wall, rail); } };
    std::vector<Lunchbox> lunchboxes;
    struct Layout { std::map<juce::String, juce::Rectangle<float>> dev; std::vector<Lunchbox> boxes; float moduleH = 0; int height = 0;
                    std::vector<std::pair<juce::String, juce::Rectangle<float>>> rows; };   // radky shora: ID 19" pristroje, "" = lunchbox
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> rows;
    Layout computeLayout (int width, int availableHeight) const;

    // presouvani pristroju
    juce::String grabbed;
    juce::Point<int> grabStart, grabNow;
    juce::Image grabImage;
    juce::Point<int> grabOffset;
    juce::Rectangle<int> grabSource;
    bool grabActive = false;
    int dropIndex = -1, dropSlot = -1;
    bool dropValid = true;
    juce::Rectangle<int> dropMarker;

    Update update;
    bool updating = false;
    juce::String librarySignature;
    int savedFlash = 0, tick = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    void changeListenerCallback (juce::ChangeBroadcaster*) override   { refreshAll(); }
    void timerCallback() override;

    void syncComponents();
    void layoutRack (juce::Rectangle<int> area);
    int rackHeight (int width, int availableHeight) const;

    void rebuildSnapshotBox();
    void refreshTopBar();
    void edited();
    void showTip (PanelView&, const juce::String&, juce::Point<int>, int radius);
    void handleGrab (const juce::String& deviceId, const juce::MouseEvent&, int phase);
    void computeDrop (juce::Point<int>);
    void entryLeave (PanelView*, bool forward);
    void showAddMenu();
    void removeDevice (const juce::String& id);
    void showLunchboxMenu();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int slotAt (float x) const;                  // slot lunchboxu pod x (-1 = mimo)

    void doSave();
    void showMenu();
    void showDeviceMenu (const juce::String& deviceId);
    void showGroupMenu (const juce::String& groupId);
    void showVersionMenu (juce::PopupMenu&);
    void addGroup();
    void askText (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done);
    void setStatus (const juce::String& text, juce::Colour = juce::Colour (0xff8b929e), int seconds = 6);
    void showReview (const juce::StringArray& instances);
    void loadSavedPhotos();
    bool showPatchbay = false;
    int statusTicks = 0;
    struct Constrainer : juce::ComponentBoundsConstrainer
    {
        RackMemoryEditor* ed = nullptr;
        void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous, const juce::Rectangle<int>& limits,
                          bool top, bool left, bool bottom, bool right) override;
    } constrainer;
    void choosePhoto (const juce::String& deviceId);
    void importPhoto (const juce::File&, const juce::String& deviceId);
    void refreshPhotos();
    void showError (const juce::String&);
    void showInfo (const juce::String&);
    void checkForUpdate();
    void runUpdate (const juce::File& package);
    void startUpdate();
    void refreshUpdateButton();
    juce::SharedResourcePointer<rm::Updater> updater;
    juce::uint32 updaterRevision = 0;
    bool updateRequested = false, manualCheck = false, libraryCheckRequested = false;
    juce::String lastUpdateError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackMemoryEditor)
};
