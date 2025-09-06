
#include <tesla.hpp>
#include <switch.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <log.h>
#include <iostream>
#include <HdlsStatePairQueue.h>
#include "../i18n.hpp"
using namespace std;
static string g_filePath;
struct ButtonGlyph {
    u64 button;
    const char* glyph;
};
constexpr array<ButtonGlyph, 18> ButtonGlyphArr = {{
    { HidNpadButton_A, "\uE0A0" },
    { HidNpadButton_B, "\uE0A1" },
    { HidNpadButton_X, "\uE0A2" },
    { HidNpadButton_Y, "\uE0A3" },
    { HidNpadButton_StickL, "\uE0C4" },
    { HidNpadButton_StickR, "\uE0C5" },
    { HidNpadButton_L, "\uE0A4" },
    { HidNpadButton_R, "\uE0A5" },
    { HidNpadButton_ZL, "\uE0A6" },
    { HidNpadButton_ZR, "\uE0A7" },
    { HidNpadButton_Plus, "\uE0B5" },
    { HidNpadButton_Minus, "\uE0B6" },
    { HidNpadButton_Left, "\uE0B1" },
    { HidNpadButton_Up, "\uE0AF" },
    { HidNpadButton_Right, "\uE0B2" },
    { HidNpadButton_Down, "\uE0B0" }
}};

constexpr array<ButtonGlyph, 18> PseudoButtonGlyphArr = {{
    { HidNpadButton_StickLLeft, "\uE091" },
    { HidNpadButton_StickLUp, "\uE092" },
    { HidNpadButton_StickLRight, "\uE090" },
    { HidNpadButton_StickLDown, "\uE093" },
    
    // { HidNpadButton_StickRLeft, "\uE091" },
    // { HidNpadButton_StickRUp, "\uE092" },
    // { HidNpadButton_StickRRight, "\uE090" },
    // { HidNpadButton_StickRDown, "\uE093" }
    // libnx type mismatch
    { HidNpadButton_LeftSL, "\uE091" },
    { HidNpadButton_LeftSR, "\uE092" },
    { HidNpadButton_RightSL, "\uE090" },
    { HidNpadButton_RightSR, "\uE093" }
}};

static string maskToGlyph(const u64 mask) {
    string glyphCombo;
    for (const ButtonGlyph &buttonGlyph : ButtonGlyphArr) {
        if (mask & buttonGlyph.button) {
            // if (!glyphCombo.empty())
            //     glyphCombo += "\u002B"; // \u002B
            glyphCombo += buttonGlyph.glyph;
        }
    }
    return glyphCombo;
}
static string pseudoMaskToGlyph(const u64 mask) {
    string glyphCombo;
    for (const ButtonGlyph &buttonGlyph : PseudoButtonGlyphArr) {
        if (mask & buttonGlyph.button) {
            // if (!glyphCombo.empty())
            //     glyphCombo += "\u002B"; // \u002B
            glyphCombo += buttonGlyph.glyph;
        }
    }
    glyphCombo = glyphCombo == "\uE091\uE092" ? "\uE097" : glyphCombo;
    glyphCombo = glyphCombo == "\uE092\uE090" ? "\uE094" : glyphCombo;
    glyphCombo = glyphCombo == "\uE090\uE093" ? "\uE095" : glyphCombo;
    glyphCombo = glyphCombo == "\uE091\uE093" ? "\uE096" : glyphCombo;
    return glyphCombo;
}

class StatePairListItem : public tsl::elm::ListItem
{
public:
    HdlsStatePair statePair;
    StatePairListItem(HdlsStatePair& statePair, const std::string& text, const std::string& value = "")
    : ListItem(text, value), statePair(statePair) {
        this->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_X) {
                this->statePair.count = 0;
                this->setValue(to_string(0));
            }
            return false;
        });
    }
    
    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override {
        static u32 tick = 0;

        if (keysHeld & HidNpadButton_AnyLeft && keysHeld & HidNpadButton_AnyRight) {
            tick = 0;
            return true;
        }

        if (keysHeld & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
            if ((tick == 0 || tick > 20) && (tick % 3) == 0) {
                if (keysHeld & HidNpadButton_AnyLeft && this->statePair.count > 0) {
                    this->statePair.count = max(0, --this->statePair.count);
                    this->setValue(to_string(this->statePair.count));
                } else if (keysHeld & HidNpadButton_AnyRight) {
                    this->statePair.count++;
                    this->setValue(to_string(this->statePair.count));
                } else {
                    return false;
                }
            }
            tick++;
            return true;
        } else {
            tick = 0;
        }
        return false;
    }
};

class GuiMacroFileView : public tsl::Gui
{
private:
    tsl::elm::OverlayFrame *frame;
    tsl::elm::List *list;
    // pointer to the currently-displayed ListItem for the selected combo
    tsl::elm::ListItem *item;

public:
    GuiMacroFileView(tsl::elm::ListItem *item, string filePath) : item(item) {
        g_filePath = filePath;
        log_info("GuiMacroFileView created, item=%p:%s filePath=%p:%s", (void*)item, item ? item->getText().c_str() : "(null)", (void*)&g_filePath, g_filePath.c_str());
    }

    virtual tsl::elm::Element *createUI() override
    {
        // Create the UI elements
        frame = new tsl::elm::OverlayFrame(i18n_getString("A001"),
            i18n_getString("A00I")+"\n\uE0ED\uE0EE "+i18n_getString("A00P")+"\n\uE0E2  "+i18n_getString("A00Q"),
            std::string("\uE0E1  ")+i18n_getString("A00H")+std::string("     \uE0F0  ")+i18n_getString("A00N")+std::string("     \uE0EF  ")+i18n_getString("A00O"));
        list = new tsl::elm::List();
        initContent();
        frame->setContent(list);
        return frame;
    }

    virtual void initContent() {
        tsl::Gui::removeFocus(nullptr);
        list->clear();
        // list->addItem(new tsl::elm::ListItem("1\uE060\uE061\uE062\uE063\uE064\uE065\uE066\uE067\uE068\uE069\uE06A\uE06B\uE06C\uE06D\uE06E\uE06F"));
        // list->addItem(new tsl::elm::ListItem("2\uE070\uE071\uE072\uE073\uE074\uE075\uE076\uE077\uE078 \uE079\uE07A\uE07B\uE07C \uE07D\uE07E\uE07F"));
        // list->addItem(new tsl::elm::ListItem("3\uE080\uE081\uE082\uE083\uE084\uE085\uE086\uE087\uE088\uE089 \uE08A\uE08B \uE08C\uE08D\uE08E\uE08F"));
        // list->addItem(new tsl::elm::ListItem("4\uE090\uE091\uE092\uE093\uE094\uE095\uE096\uE097\uE098\uE099\uE09A\uE09B\uE09C\uE09D\uE09E\uE09F"));
        // list->addItem(new tsl::elm::ListItem("5\uE0A0\uE0A1\uE0A2\uE0A3\uE0A4\uE0A5\uE0A6\uE0A7\uE0A8\uE0A9\uE0AA\uE0AB\uE0AC\uE0AD\uE0AE \uE0AF"));
        // list->addItem(new tsl::elm::ListItem("6\uE0B0\uE0B1\uE0B2 \uE0B3\uE0B4\uE0B5\uE0B6\uE0B7\uE0B8\uE0B9\uE0BA\uE0BB\uE0BC\uE0BD\uE0BE\uE0BF"));
        // list->addItem(new tsl::elm::ListItem("7\uE0C0\uE0C1\uE0C2\uE0C3 \uE0C4\uE0C5 \uE0C6\uE0C7\uE0C8\uE0C9\uE0CA\uE0CB\uE0CC\uE0CD\uE0CE\uE0CF"));
        // list->addItem(new tsl::elm::ListItem("8\uE0D0\uE0D1\uE0D2\uE0D3\uE0D4\uE0D5\uE0D6\uE0D7\uE0D8\uE0D9\uE0DA\uE0DB\uE0DC\uE0DD\uE0DE\uE0DF"));
        // list->addItem(new tsl::elm::ListItem("9\uE0E0\uE0E1\uE0E2\uE0E3\uE0E4\uE0E5\uE0E6\uE0E7\uE0E8\uE0E9\uE0EA\uE0EB\uE0EC\uE0ED\uE0EE\uE0EF"));
        // list->addItem(new tsl::elm::ListItem("10\uE0F0\uE0F1\uE0F2\uE0F3\uE0F4\uE0F5\uE0F6\uE0F7\uE0F8\uE0F9\uE0FA\uE0FB\uE0FC\uE0FD\uE0FE\uE0FF"));
        // list->addItem(new tsl::elm::ListItem("11\uE100\uE101\uE102\uE103\uE104\uE105\uE106\uE107\uE108\uE109\uE10A\uE10B\uE10C\uE10D\uE10E\uE10F"));
        // list->addItem(new tsl::elm::ListItem("12\uE110\uE111\uE112\uE113\uE114\uE115\uE116\uE117\uE118\uE119\uE11A\uE11B\uE11C\uE11D\uE11E\uE11F"));
        // list->addItem(new tsl::elm::ListItem("13\uE120\uE121\uE122\uE123\uE124\uE125\uE126\uE127\uE128\uE129\uE12A\uE12B\uE12C\uE12D\uE12E\uE12F"));

        FILE* file = fopen(("sdmc:" + g_filePath).c_str(), "rb");
        if (!file)
        {
            log_error("Failed to open file: %s", ("sdmc:" + g_filePath).c_str());
            return;
        }
        do {
            HdlsStatePair statePair = {0};
            size_t n = fread(&statePair, 1, sizeof(HdlsStatePair), file);
            if (n != sizeof(HdlsStatePair)) break;
            string lb = maskToGlyph(statePair.left.buttons);
            string rb = maskToGlyph(statePair.right.buttons);
            string ls = (statePair.left.analog_stick_l.x != 0 || statePair.left.analog_stick_l.y != 0) ?
                string("\uE0C1").append(pseudoMaskToGlyph(statePair.left.buttons)) : ""; //left stick position not 0
            string rs = (statePair.right.analog_stick_r.x != 0 || statePair.left.analog_stick_r.y != 0) ?
                string("\uE0C2").append(pseudoMaskToGlyph(statePair.right.buttons)) : ""; // right stick position not 0
            string text;
            text.append(lb.c_str());text.append(rb.c_str());text.append(ls.c_str());text.append(rs.c_str());
            tsl::elm::ListItem *item = new StatePairListItem(statePair, text.empty() ? " " : text, to_string(statePair.count));
            list->addItem(item);
        } while(1);
        fclose(file);
        frame->setClickListener([this](u64 keys){
            if (keys & HidNpadButton_Minus) {
                this->initContent();
            } else if (keys & HidNpadButton_Plus) {
                FILE* file = fopen(("sdmc:" + g_filePath).c_str(), "wb");
                u32 index = 0; StatePairListItem *item;
                while((item = (StatePairListItem*)this->list->getItemAtIndex(index))) {
                    if (item->statePair.count == 0) {
                        index++;
                        continue;
                    }
                    fwrite(&item->statePair, 1, sizeof(item->statePair), file);
                    index++;
                }
                fclose(file);
                this->initContent();
            }
            return false;
        });
    }
};