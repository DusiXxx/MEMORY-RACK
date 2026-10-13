#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "Updater.h"

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

    std::function<void (const juce::File&)> onPhotoDropped;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    std::function<void()> onEdit;                                              // hodnota zmenena
    std::function<void (const juce::String&, juce::Point<int>, int)> onHover;  // text, stred ovladace, polomer
    std::function<void (const juce::MouseEvent&, int phase)> onGrab;           // presun pristroje: 0 start, 1 tah, 2 konec
    std::function<void (PanelView*, bool forward)> onEntryLeave;               // Tab za posledni / pred prvni ovladac

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
    juce::Rectangle<float> panel;
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
    std::function<void()> onClick, onRemove, onView;

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
// Velka fotka pres rack (klik kamkoli = zavrit). Vzhled panelu se podle fotky nemeni.
class PhotoViewer : public juce::Component
{
public:
    juce::Image image;
    juce::String caption;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override   { setVisible (false); }
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
    bool updateRequested = false, manualCheck = false;
    juce::String lastUpdateError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackMemoryEditor)
};
