
#include <tesla.hpp>
#include <switch.h>
#include <log.h>
#include <curl/curl.h>
#include "../i18n.hpp"
#include <json.hpp>
#include <iostream>
#include <vector>
#include <HdlsStatePairQueue.h>
#include "util.hpp"
#include "../config.hpp"

using json = nlohmann::json;
using namespace std;

class ComboSet : public tsl::Gui
{
private:
    tsl::elm::ListItem *item;
    HdlsStatePair *statePair;
    u64 *mask;
    HdlsStatePair newStatePair;
    string text;
public:
    ComboSet(tsl::elm::ListItem *item, HdlsStatePair *statePair, u64 *mask = 0x0) : item(item), statePair(statePair), mask(mask) {
        if (statePair != nullptr) {
            newStatePair = *statePair;
        }
        newStatePair.left.buttons = 0;
        newStatePair.left.analog_stick_l = { 0, 0 };
        newStatePair.left.analog_stick_r = { 0, 0 };
        newStatePair.left.six_axis_sensor_acceleration = { 0, 0, 0 };
        newStatePair.left.six_axis_sensor_angle = { 0, 0, 0 };
        newStatePair.right.buttons = 0;
        newStatePair.right.analog_stick_l = { 0, 0 };
        newStatePair.right.analog_stick_r = { 0, 0 };
        newStatePair.right.six_axis_sensor_acceleration = { 0, 0, 0 };
        newStatePair.right.six_axis_sensor_angle = { 0, 0, 0 };

    }

    virtual tsl::elm::Element *createUI() override
    {
        // Create the UI elements
        tsl::elm::OverlayFrame *frame = new tsl::elm::OverlayFrame(i18n_getString("A003"),
            i18n_getString("A00Z")+"\n"+i18n_getString("B001")+"\uE0B5\uE0B6",
            std::string("\uE0F0  ")+i18n_getString("A00N")+std::string("     \uE0EF  ")+i18n_getString("A00O"));
        tsl::elm::CustomDrawer *customDrawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32 h) {
            // Custom drawing logic for the combo set
            // You can use the item member variable here
            string lb = maskToGlyph(this->newStatePair.left.buttons);
            string rb = maskToGlyph(this->newStatePair.right.buttons);
            string ls = (this->newStatePair.left.analog_stick_l.x != 0 || this->newStatePair.left.analog_stick_l.y != 0) ?
                string("\uE0C1").append(pseudoMaskToGlyph(this->newStatePair.left.buttons)) : ""; //left stick position not 0
            string rs = (this->newStatePair.right.analog_stick_r.x != 0 || this->newStatePair.right.analog_stick_r.y != 0) ?
                string("\uE0C2").append(pseudoMaskToGlyph(this->newStatePair.right.buttons)) : ""; // right stick position not 0
            text = "";
            text.append(lb.c_str());text.append(rb.c_str());text.append(ls.c_str());text.append(rs.c_str());
            if (!text.empty()) {
            // renderer->drawString("Hello :)", false, x + 250, y + 70, 20, renderer->a(0xFF0F));
                log_info("size: %d,", text.size());
                size_t rows = text.size() / 3 / 3 + 1;
                for (size_t i = 0; i < rows; ++i) {
                    string temp = text.substr(i * 3 * 3, 3 * 3);
                    r->drawString(temp.c_str(), false, x + 30, y + 120 + (i * 110), 100, a(tsl::style::color::ColorText));
                }
            }
        });
        frame->setContent(customDrawer);
        return frame;
    }
    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override {
        if (keysDown & HidNpadButton_Plus) {
            // save and exit
            if (statePair != nullptr) {
                statePair->left = newStatePair.left;
                statePair->right = newStatePair.right;
                item->setText(this->text.empty() ? " " : this->text);
            } else {
                *mask = ((newStatePair.left.buttons | newStatePair.right.buttons) & 0xFFFF);
                item->setValue(maskToGlyph(*mask));
                saveConfig();
            }
            tsl::goBack();
            return true;
        } else if (keysDown & HidNpadButton_Minus) {
            // discard and exit
            tsl::goBack();
            return true;
        }
        newStatePair.left.analog_stick_l.x = abs(newStatePair.left.analog_stick_l.x) <= abs(leftJoyStick.x) ? leftJoyStick.x : newStatePair.left.analog_stick_l.x;
        newStatePair.left.analog_stick_l.y = abs(newStatePair.left.analog_stick_l.y) <= abs(leftJoyStick.y) ? leftJoyStick.y : newStatePair.left.analog_stick_l.y;
        newStatePair.right.analog_stick_r.x = abs(newStatePair.right.analog_stick_r.x) <= abs(rightJoyStick.x) ? rightJoyStick.x : newStatePair.right.analog_stick_r.x;
        newStatePair.right.analog_stick_r.y = abs(newStatePair.right.analog_stick_r.y) <= abs(rightJoyStick.y) ? rightJoyStick.y : newStatePair.right.analog_stick_r.y;
        u64 leftDown, rightDown;
        splitMask(keysDown, &leftDown, &rightDown);
        newStatePair.left.buttons |= leftDown;
        newStatePair.right.buttons |= rightDown;
        return true;
    }
};